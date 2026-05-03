package cluster

import (
	"bytes"
	"context"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"log"
	"math"
	mrand "math/rand"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"time"
)

type Node struct {
	mu          sync.Mutex
	id          int
	addr        string
	peers       map[int]string
	peerIDs     []int
	client      *http.Client
	server      *http.Server
	dataFile    string
	role        Role
	currentTerm int
	votedFor    int
	leaderID    int
	log         []LogEntry
	commitIndex int
	lastApplied int
	store       *Store
	nextIndex   map[int]int
	matchIndex  map[int]int
	results     map[int]ApplyResult
	failed      bool
	lastContact time.Time
	readCursor  int
	done        chan struct{}
}

type persistentState struct {
	CurrentTerm int        `json:"current_term"`
	VotedFor    int        `json:"voted_for"`
	Log         []LogEntry `json:"log"`
	CommitIndex int        `json:"commit_index"`
}

func NewNode(id int, addr string, peers map[int]string, dataDir string) (*Node, error) {
	n := &Node{
		id:          id,
		addr:        addr,
		peers:       peers,
		peerIDs:     sortedPeerIDs(peers),
		client:      &http.Client{Timeout: 450 * time.Millisecond},
		dataFile:    filepath.Join(dataDir, fmt.Sprintf("node-%d.json", id)),
		role:        Follower,
		votedFor:    -1,
		leaderID:    -1,
		store:       NewStore(),
		nextIndex:   make(map[int]int),
		matchIndex:  make(map[int]int),
		results:     make(map[int]ApplyResult),
		lastContact: time.Now(),
		done:        make(chan struct{}),
	}
	if err := n.load(); err != nil {
		return nil, err
	}
	mux := http.NewServeMux()
	mux.HandleFunc("/", n.handleHTTP)
	n.server = &http.Server{Addr: addr, Handler: mux}
	return n, nil
}

func (n *Node) Start() {
	go func() {
		if err := n.server.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
			log.Printf("node %d listen error: %v", n.id, err)
		}
	}()
	go n.electionLoop()
	go n.heartbeatLoop()
}

func (n *Node) Shutdown(ctx context.Context) error {
	close(n.done)
	return n.server.Shutdown(ctx)
}

func (n *Node) URL() string {
	return "http://" + n.addr
}

func (n *Node) electionLoop() {
	rng := mrand.New(mrand.NewSource(time.Now().UnixNano() + int64(n.id)*1000))
	for {
		timeout := time.Duration(850+rng.Intn(650)) * time.Millisecond
		timer := time.NewTimer(timeout)
		select {
		case <-n.done:
			timer.Stop()
			return
		case <-timer.C:
			n.mu.Lock()
			shouldStart := !n.failed && n.role != Leader && time.Since(n.lastContact) >= timeout
			n.mu.Unlock()
			if shouldStart {
				n.startElection()
			}
		}
	}
}

func (n *Node) heartbeatLoop() {
	ticker := time.NewTicker(180 * time.Millisecond)
	defer ticker.Stop()
	for {
		select {
		case <-n.done:
			return
		case <-ticker.C:
			n.mu.Lock()
			isLeader := !n.failed && n.role == Leader
			n.mu.Unlock()
			if isLeader {
				n.broadcastAppendEntries()
			}
		}
	}
}

func (n *Node) startElection() {
	n.mu.Lock()
	if n.failed {
		n.mu.Unlock()
		return
	}
	n.role = Candidate
	n.currentTerm++
	term := n.currentTerm
	n.votedFor = n.id
	n.leaderID = -1
	n.lastContact = time.Now()
	lastIndex, lastTerm := n.lastLogInfoLocked()
	_ = n.saveLocked()
	n.mu.Unlock()

	votes := 1
	var votesMu sync.Mutex
	var wg sync.WaitGroup
	for peerID, peerAddr := range n.peers {
		wg.Add(1)
		go func(peerID int, peerAddr string) {
			defer wg.Done()
			req := requestVoteRequest{Term: term, CandidateID: n.id, LastLogIndex: lastIndex, LastLogTerm: lastTerm}
			var resp requestVoteResponse
			if err := n.postJSON(peerAddr, "/internal/vote", req, &resp); err != nil {
				return
			}
			n.mu.Lock()
			if resp.Term > n.currentTerm {
				n.becomeFollowerLocked(resp.Term, -1)
				_ = n.saveLocked()
				n.mu.Unlock()
				return
			}
			n.mu.Unlock()
			if resp.VoteGranted {
				votesMu.Lock()
				votes++
				votesMu.Unlock()
			}
		}(peerID, peerAddr)
	}
	wg.Wait()

	n.mu.Lock()
	defer n.mu.Unlock()
	if n.failed || n.role != Candidate || n.currentTerm != term {
		return
	}
	if votes >= n.majority() {
		n.role = Leader
		n.leaderID = n.id
		next := n.lastLogIndexLocked() + 1
		for peerID := range n.peers {
			n.nextIndex[peerID] = next
			n.matchIndex[peerID] = 0
		}
		log.Printf("node %d became leader for term %d", n.id, term)
	}
}

func (n *Node) broadcastAppendEntries() {
	var wg sync.WaitGroup
	for peerID := range n.peers {
		wg.Add(1)
		go func(peerID int) {
			defer wg.Done()
			n.sendAppendEntries(peerID)
		}(peerID)
	}
	wg.Wait()
}

func (n *Node) sendAppendEntries(peerID int) bool {
	n.mu.Lock()
	if n.failed || n.role != Leader {
		n.mu.Unlock()
		return false
	}
	next := n.nextIndex[peerID]
	if next < 1 {
		next = 1
	}
	prevIndex := next - 1
	req := appendEntriesRequest{
		Term:         n.currentTerm,
		LeaderID:     n.id,
		PrevLogIndex: prevIndex,
		PrevLogTerm:  n.termAtLocked(prevIndex),
		Entries:      append([]LogEntry(nil), n.entriesFromLocked(next)...),
		LeaderCommit: n.commitIndex,
	}
	peerAddr := n.peers[peerID]
	n.mu.Unlock()

	var resp appendEntriesResponse
	if err := n.postJSON(peerAddr, "/internal/append", req, &resp); err != nil {
		return false
	}

	n.mu.Lock()
	defer n.mu.Unlock()
	if resp.Term > n.currentTerm {
		n.becomeFollowerLocked(resp.Term, -1)
		_ = n.saveLocked()
		return false
	}
	if n.role != Leader || req.Term != n.currentTerm {
		return false
	}
	if resp.Success {
		n.matchIndex[peerID] = resp.MatchIndex
		n.nextIndex[peerID] = resp.MatchIndex + 1
		return true
	}
	if n.nextIndex[peerID] > 1 {
		n.nextIndex[peerID]--
	}
	return false
}

func (n *Node) ApplyCommand(cmd Command) (ApplyResult, error) {
	n.mu.Lock()
	if n.failed || n.role != Leader {
		n.mu.Unlock()
		return ApplyResult{}, errNotLeader
	}
	entry := LogEntry{Index: n.lastLogIndexLocked() + 1, Term: n.currentTerm, Command: cmd}
	n.log = append(n.log, entry)
	_ = n.saveLocked()
	n.mu.Unlock()

	deadline := time.Now().Add(4 * time.Second)
	for time.Now().Before(deadline) {
		n.broadcastAppendEntries()
		n.mu.Lock()
		if n.role != Leader || n.failed {
			n.mu.Unlock()
			return ApplyResult{}, errNotLeader
		}
		replicated := 1
		for _, match := range n.matchIndex {
			if match >= entry.Index {
				replicated++
			}
		}
		if replicated >= n.majority() {
			if entry.Index > n.commitIndex {
				n.commitIndex = entry.Index
				n.applyCommittedLocked()
				_ = n.saveLocked()
			}
			result := n.results[entry.Index]
			n.mu.Unlock()
			n.broadcastAppendEntries()
			return result, nil
		}
		n.mu.Unlock()
		time.Sleep(55 * time.Millisecond)
	}
	return ApplyResult{}, errors.New("write timed out before majority replication")
}

func (n *Node) handleHTTP(w http.ResponseWriter, r *http.Request) {
	if r.URL.Path == "/internal/append" {
		n.handleAppendEntries(w, r)
		return
	}
	if r.URL.Path == "/internal/vote" {
		n.handleRequestVote(w, r)
		return
	}
	if r.URL.Path == "/status" {
		n.handleStatus(w, r)
		return
	}
	if r.URL.Path == "/admin/fail" || r.URL.Path == "/admin/recover" {
		n.handleAdmin(w, r)
		return
	}
	n.mu.Lock()
	failed := n.failed
	n.mu.Unlock()
	if failed {
		writeJSON(w, http.StatusServiceUnavailable, map[string]string{"error": "node is failed"})
		return
	}
	switch {
	case r.URL.Path == "/items":
		n.handleItems(w, r)
	case strings.HasPrefix(r.URL.Path, "/items/"):
		n.handleItem(w, r)
	case strings.HasPrefix(r.URL.Path, "/cas/"):
		n.handleCAS(w, r)
	default:
		writeJSON(w, http.StatusNotFound, map[string]string{"error": "not found"})
	}
}

func (n *Node) handleAppendEntries(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		w.WriteHeader(http.StatusMethodNotAllowed)
		return
	}
	var req appendEntriesRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": err.Error()})
		return
	}
	n.mu.Lock()
	defer n.mu.Unlock()
	if n.failed {
		writeJSON(w, http.StatusServiceUnavailable, map[string]string{"error": "node is failed"})
		return
	}
	resp := appendEntriesResponse{Term: n.currentTerm}
	if req.Term < n.currentTerm {
		writeJSON(w, http.StatusOK, resp)
		return
	}
	if req.Term > n.currentTerm {
		n.becomeFollowerLocked(req.Term, -1)
	}
	n.role = Follower
	n.leaderID = req.LeaderID
	n.lastContact = time.Now()
	resp.Term = n.currentTerm
	if !n.hasEntryLocked(req.PrevLogIndex, req.PrevLogTerm) {
		resp.MatchIndex = min(n.lastLogIndexLocked(), req.PrevLogIndex-1)
		_ = n.saveLocked()
		writeJSON(w, http.StatusOK, resp)
		return
	}
	for i, entry := range req.Entries {
		if n.termAtLocked(entry.Index) != entry.Term {
			n.truncateFromLocked(entry.Index)
			n.log = append(n.log, req.Entries[i:]...)
			break
		}
	}
	if req.LeaderCommit > n.commitIndex {
		n.commitIndex = min(req.LeaderCommit, n.lastLogIndexLocked())
		n.applyCommittedLocked()
	}
	resp.Success = true
	resp.MatchIndex = n.lastLogIndexLocked()
	_ = n.saveLocked()
	writeJSON(w, http.StatusOK, resp)
}

func (n *Node) handleRequestVote(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		w.WriteHeader(http.StatusMethodNotAllowed)
		return
	}
	var req requestVoteRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": err.Error()})
		return
	}
	n.mu.Lock()
	defer n.mu.Unlock()
	if n.failed {
		writeJSON(w, http.StatusServiceUnavailable, map[string]string{"error": "node is failed"})
		return
	}
	resp := requestVoteResponse{Term: n.currentTerm}
	if req.Term < n.currentTerm {
		writeJSON(w, http.StatusOK, resp)
		return
	}
	if req.Term > n.currentTerm {
		n.becomeFollowerLocked(req.Term, -1)
	}
	upToDate := n.candidateUpToDateLocked(req.LastLogIndex, req.LastLogTerm)
	if (n.votedFor == -1 || n.votedFor == req.CandidateID) && upToDate {
		n.votedFor = req.CandidateID
		n.lastContact = time.Now()
		resp.VoteGranted = true
	}
	resp.Term = n.currentTerm
	_ = n.saveLocked()
	writeJSON(w, http.StatusOK, resp)
}

func (n *Node) handleItems(w http.ResponseWriter, r *http.Request) {
	switch r.Method {
	case http.MethodGet:
		if n.redirectReadIfLeader(w, r) {
			return
		}
		n.mu.Lock()
		items := n.store.All()
		n.mu.Unlock()
		writeJSON(w, http.StatusOK, items)
	case http.MethodPost:
		value, ok := readRawBody(w, r)
		if !ok {
			return
		}
		id := randomID()
		n.writeCommand(w, r, Command{Type: CmdCreate, ID: id, Value: &value})
	default:
		w.WriteHeader(http.StatusMethodNotAllowed)
	}
}

func (n *Node) handleItem(w http.ResponseWriter, r *http.Request) {
	id := strings.TrimPrefix(r.URL.Path, "/items/")
	if id == "" || strings.Contains(id, "/") {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "invalid resource id"})
		return
	}
	switch r.Method {
	case http.MethodGet:
		if n.redirectReadIfLeader(w, r) {
			return
		}
		n.mu.Lock()
		value, ok := n.store.Get(id)
		n.mu.Unlock()
		if !ok {
			writeJSON(w, http.StatusNotFound, map[string]string{"error": "resource not found"})
			return
		}
		writeRawJSON(w, http.StatusOK, value)
	case http.MethodPut:
		value, ok := readRawBody(w, r)
		if !ok {
			return
		}
		n.writeCommand(w, r, Command{Type: CmdPut, ID: id, Value: &value})
	case http.MethodPatch:
		patch, ok := readRawBody(w, r)
		if !ok {
			return
		}
		n.writeCommand(w, r, Command{Type: CmdPatch, ID: id, Patch: &patch})
	case http.MethodDelete:
		n.writeCommand(w, r, Command{Type: CmdDelete, ID: id})
	default:
		w.WriteHeader(http.StatusMethodNotAllowed)
	}
}

func (n *Node) handleCAS(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		w.WriteHeader(http.StatusMethodNotAllowed)
		return
	}
	id := strings.TrimPrefix(r.URL.Path, "/cas/")
	if id == "" || strings.Contains(id, "/") {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "invalid resource id"})
		return
	}
	var body struct {
		Expected *json.RawMessage `json:"expected"`
		Update   *json.RawMessage `json:"update"`
	}
	if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": err.Error()})
		return
	}
	n.writeCommand(w, r, Command{Type: CmdCAS, ID: id, Expected: body.Expected, Update: body.Update})
}

func (n *Node) writeCommand(w http.ResponseWriter, r *http.Request, cmd Command) {
	result, err := n.ApplyCommand(cmd)
	if errors.Is(err, errNotLeader) {
		n.redirectWriteToLeader(w, r)
		return
	}
	if err != nil {
		writeJSON(w, http.StatusServiceUnavailable, map[string]string{"error": err.Error()})
		return
	}
	if result.Code == http.StatusNoContent {
		w.WriteHeader(result.Code)
		return
	}
	if result.Error != "" {
		writeJSON(w, result.Code, result)
		return
	}
	writeJSON(w, result.Code, result)
}

func (n *Node) redirectReadIfLeader(w http.ResponseWriter, r *http.Request) bool {
	n.mu.Lock()
	defer n.mu.Unlock()
	if n.role != Leader || len(n.peerIDs) == 0 {
		return false
	}
	peerID := n.peerIDs[n.readCursor%len(n.peerIDs)]
	n.readCursor++
	location := "http://" + n.peers[peerID] + r.URL.RequestURI()
	http.Redirect(w, r, location, http.StatusFound)
	return true
}

func (n *Node) redirectWriteToLeader(w http.ResponseWriter, r *http.Request) {
	n.mu.Lock()
	leaderID := n.leaderID
	var location string
	if leaderID == n.id {
		location = n.URL() + r.URL.RequestURI()
	} else if addr, ok := n.peers[leaderID]; ok {
		location = "http://" + addr + r.URL.RequestURI()
	}
	n.mu.Unlock()
	if location == "" {
		writeJSON(w, http.StatusServiceUnavailable, map[string]string{"error": "leader is unknown"})
		return
	}
	w.Header().Set("Location", location)
	writeJSON(w, http.StatusTemporaryRedirect, map[string]string{"leader": location})
}

func (n *Node) handleStatus(w http.ResponseWriter, r *http.Request) {
	n.mu.Lock()
	defer n.mu.Unlock()
	writeJSON(w, http.StatusOK, map[string]any{
		"id":           n.id,
		"addr":         n.addr,
		"role":         n.role,
		"term":         n.currentTerm,
		"leader_id":    n.leaderID,
		"failed":       n.failed,
		"commit_index": n.commitIndex,
		"last_log":     n.lastLogIndexLocked(),
		"items":        n.store.All(),
	})
}

func (n *Node) handleAdmin(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		w.WriteHeader(http.StatusMethodNotAllowed)
		return
	}
	n.mu.Lock()
	defer n.mu.Unlock()
	switch r.URL.Path {
	case "/admin/fail":
		n.failed = true
		n.role = Follower
	case "/admin/recover":
		n.failed = false
		n.role = Follower
		n.leaderID = -1
		n.lastContact = time.Now()
	}
	writeJSON(w, http.StatusOK, map[string]any{"id": n.id, "failed": n.failed, "role": n.role})
}

func (n *Node) postJSON(addr, path string, req any, resp any) error {
	body, err := json.Marshal(req)
	if err != nil {
		return err
	}
	httpResp, err := n.client.Post("http://"+addr+path, "application/json", bytes.NewReader(body))
	if err != nil {
		return err
	}
	defer httpResp.Body.Close()
	if httpResp.StatusCode >= 500 {
		return fmt.Errorf("peer %s returned %s", addr, httpResp.Status)
	}
	return json.NewDecoder(httpResp.Body).Decode(resp)
}

func (n *Node) becomeFollowerLocked(term int, leaderID int) {
	n.role = Follower
	n.currentTerm = term
	n.votedFor = -1
	n.leaderID = leaderID
	n.lastContact = time.Now()
}

func (n *Node) majority() int {
	return int(math.Floor(float64(len(n.peers)+1)/2)) + 1
}

func (n *Node) lastLogInfoLocked() (int, int) {
	lastIndex := n.lastLogIndexLocked()
	return lastIndex, n.termAtLocked(lastIndex)
}

func (n *Node) lastLogIndexLocked() int {
	if len(n.log) == 0 {
		return 0
	}
	return n.log[len(n.log)-1].Index
}

func (n *Node) termAtLocked(index int) int {
	if index == 0 {
		return 0
	}
	if index < 0 || index > len(n.log) {
		return -1
	}
	return n.log[index-1].Term
}

func (n *Node) entriesFromLocked(index int) []LogEntry {
	if index <= 0 {
		index = 1
	}
	if index > len(n.log) {
		return nil
	}
	return n.log[index-1:]
}

func (n *Node) hasEntryLocked(index, term int) bool {
	if index == 0 {
		return true
	}
	return n.termAtLocked(index) == term
}

func (n *Node) truncateFromLocked(index int) {
	if index <= 0 {
		n.log = nil
		n.commitIndex = 0
		n.lastApplied = 0
		n.store = NewStore()
		return
	}
	if index <= len(n.log) {
		n.log = n.log[:index-1]
	}
	if n.commitIndex >= index {
		n.commitIndex = index - 1
		n.lastApplied = 0
		n.store = NewStore()
		n.applyCommittedLocked()
	}
}

func (n *Node) candidateUpToDateLocked(lastIndex, lastTerm int) bool {
	myLastIndex, myLastTerm := n.lastLogInfoLocked()
	if lastTerm != myLastTerm {
		return lastTerm > myLastTerm
	}
	return lastIndex >= myLastIndex
}

func (n *Node) applyCommittedLocked() {
	for n.lastApplied < n.commitIndex && n.lastApplied < len(n.log) {
		entry := n.log[n.lastApplied]
		result := n.store.Apply(entry.Command)
		n.results[entry.Index] = result
		n.lastApplied = entry.Index
	}
}

func (n *Node) load() error {
	if err := os.MkdirAll(filepath.Dir(n.dataFile), 0o755); err != nil {
		return err
	}
	data, err := os.ReadFile(n.dataFile)
	if errors.Is(err, os.ErrNotExist) {
		return nil
	}
	if err != nil {
		return err
	}
	var state persistentState
	if err := json.Unmarshal(data, &state); err != nil {
		return err
	}
	n.currentTerm = state.CurrentTerm
	n.votedFor = state.VotedFor
	n.log = state.Log
	n.commitIndex = min(state.CommitIndex, len(n.log))
	n.applyCommittedLocked()
	return nil
}

func (n *Node) saveLocked() error {
	state := persistentState{
		CurrentTerm: n.currentTerm,
		VotedFor:    n.votedFor,
		Log:         n.log,
		CommitIndex: n.commitIndex,
	}
	data, err := json.MarshalIndent(state, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(n.dataFile, data, 0o644)
}

func readRawBody(w http.ResponseWriter, r *http.Request) (json.RawMessage, bool) {
	defer r.Body.Close()
	var raw json.RawMessage
	if err := json.NewDecoder(r.Body).Decode(&raw); err != nil {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": err.Error()})
		return nil, false
	}
	if !json.Valid(raw) {
		writeJSON(w, http.StatusBadRequest, map[string]string{"error": "invalid JSON body"})
		return nil, false
	}
	return raw, true
}

func writeRawJSON(w http.ResponseWriter, code int, raw json.RawMessage) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(code)
	_, _ = w.Write(raw)
}

func writeJSON(w http.ResponseWriter, code int, value any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(code)
	_ = json.NewEncoder(w).Encode(value)
}

func randomID() string {
	var buf [8]byte
	if _, err := rand.Read(buf[:]); err == nil {
		return hex.EncodeToString(buf[:])
	}
	return strconv.FormatInt(time.Now().UnixNano(), 36)
}
