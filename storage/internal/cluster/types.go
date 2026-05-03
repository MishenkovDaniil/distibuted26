package cluster

import (
	"encoding/json"
	"errors"
)

type Role string

const (
	Follower  Role = "follower"
	Candidate Role = "candidate"
	Leader    Role = "leader"
)

type CommandType string

const (
	CmdCreate CommandType = "create"
	CmdPut    CommandType = "put"
	CmdPatch  CommandType = "patch"
	CmdDelete CommandType = "delete"
	CmdCAS    CommandType = "cas"
)

type Command struct {
	Type     CommandType      `json:"type"`
	ID       string           `json:"id"`
	Value    *json.RawMessage `json:"value,omitempty"`
	Patch    *json.RawMessage `json:"patch,omitempty"`
	Expected *json.RawMessage `json:"expected,omitempty"`
	Update   *json.RawMessage `json:"update,omitempty"`
}

type LogEntry struct {
	Index   int     `json:"index"`
	Term    int     `json:"term"`
	Command Command `json:"command"`
}

type ApplyResult struct {
	Code    int              `json:"code"`
	ID      string           `json:"id,omitempty"`
	Value   *json.RawMessage `json:"value,omitempty"`
	Swapped *bool            `json:"swapped,omitempty"`
	Error   string           `json:"error,omitempty"`
}

type appendEntriesRequest struct {
	Term         int        `json:"term"`
	LeaderID     int        `json:"leader_id"`
	PrevLogIndex int        `json:"prev_log_index"`
	PrevLogTerm  int        `json:"prev_log_term"`
	Entries      []LogEntry `json:"entries"`
	LeaderCommit int        `json:"leader_commit"`
}

type appendEntriesResponse struct {
	Term       int  `json:"term"`
	Success    bool `json:"success"`
	MatchIndex int  `json:"match_index"`
}

type requestVoteRequest struct {
	Term         int `json:"term"`
	CandidateID  int `json:"candidate_id"`
	LastLogIndex int `json:"last_log_index"`
	LastLogTerm  int `json:"last_log_term"`
}

type requestVoteResponse struct {
	Term        int  `json:"term"`
	VoteGranted bool `json:"vote_granted"`
}

var errNotLeader = errors.New("node is not leader")
