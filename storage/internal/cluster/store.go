package cluster

import (
	"bytes"
	"encoding/json"
	"net/http"
	"sort"
)

type Store struct {
	items map[string]json.RawMessage
}

func NewStore() *Store {
	return &Store{items: make(map[string]json.RawMessage)}
}

func (s *Store) Get(id string) (json.RawMessage, bool) {
	value, ok := s.items[id]
	if !ok {
		return nil, false
	}
	return cloneRaw(value), true
}

func (s *Store) All() map[string]json.RawMessage {
	out := make(map[string]json.RawMessage, len(s.items))
	for id, value := range s.items {
		out[id] = cloneRaw(value)
	}
	return out
}

func (s *Store) Apply(cmd Command) ApplyResult {
	switch cmd.Type {
	case CmdCreate:
		if _, exists := s.items[cmd.ID]; exists {
			return ApplyResult{Code: http.StatusConflict, ID: cmd.ID, Error: "resource already exists"}
		}
		if cmd.Value == nil || !json.Valid(*cmd.Value) {
			return ApplyResult{Code: http.StatusBadRequest, ID: cmd.ID, Error: "invalid JSON value"}
		}
		value := cloneRaw(*cmd.Value)
		s.items[cmd.ID] = value
		return ApplyResult{Code: http.StatusCreated, ID: cmd.ID, Value: rawPtr(value)}
	case CmdPut:
		if cmd.Value == nil || !json.Valid(*cmd.Value) {
			return ApplyResult{Code: http.StatusBadRequest, ID: cmd.ID, Error: "invalid JSON value"}
		}
		value := cloneRaw(*cmd.Value)
		s.items[cmd.ID] = value
		return ApplyResult{Code: http.StatusOK, ID: cmd.ID, Value: rawPtr(value)}
	case CmdPatch:
		current, exists := s.items[cmd.ID]
		if !exists {
			return ApplyResult{Code: http.StatusNotFound, ID: cmd.ID, Error: "resource not found"}
		}
		value, err := mergePatch(current, cmd.Patch)
		if err != nil {
			return ApplyResult{Code: http.StatusBadRequest, ID: cmd.ID, Error: err.Error()}
		}
		s.items[cmd.ID] = value
		return ApplyResult{Code: http.StatusOK, ID: cmd.ID, Value: rawPtr(value)}
	case CmdDelete:
		delete(s.items, cmd.ID)
		return ApplyResult{Code: http.StatusNoContent, ID: cmd.ID}
	case CmdCAS:
		current, exists := s.items[cmd.ID]
		if !exists {
			current = json.RawMessage("null")
		}
		expected := json.RawMessage("null")
		if cmd.Expected != nil {
			expected = *cmd.Expected
		}
		swapped := jsonEqual(current, expected)
		if swapped {
			if cmd.Update == nil || bytes.Equal(bytes.TrimSpace(*cmd.Update), []byte("null")) {
				delete(s.items, cmd.ID)
				current = json.RawMessage("null")
			} else {
				value := cloneRaw(*cmd.Update)
				s.items[cmd.ID] = value
				current = value
			}
		}
		if swapped {
			return ApplyResult{Code: http.StatusOK, ID: cmd.ID, Value: rawPtr(current), Swapped: boolPtr(true)}
		}
		if exists {
			return ApplyResult{Code: http.StatusConflict, ID: cmd.ID, Value: rawPtr(current), Swapped: boolPtr(false)}
		}
		return ApplyResult{Code: http.StatusConflict, ID: cmd.ID, Swapped: boolPtr(false)}
	default:
		return ApplyResult{Code: http.StatusBadRequest, ID: cmd.ID, Error: "unknown command"}
	}
}

func mergePatch(current json.RawMessage, patch *json.RawMessage) (json.RawMessage, error) {
	if patch == nil || !json.Valid(*patch) {
		return nil, errString("invalid JSON patch")
	}
	var base map[string]json.RawMessage
	if err := json.Unmarshal(current, &base); err != nil || base == nil {
		return nil, errString("PATCH requires current resource to be a JSON object")
	}
	var delta map[string]json.RawMessage
	if err := json.Unmarshal(*patch, &delta); err != nil || delta == nil {
		return nil, errString("PATCH body must be a JSON object")
	}
	for key, value := range delta {
		base[key] = cloneRaw(value)
	}
	value, err := json.Marshal(base)
	if err != nil {
		return nil, err
	}
	return json.RawMessage(value), nil
}

func jsonEqual(a, b json.RawMessage) bool {
	na, errA := normalizeJSON(a)
	nb, errB := normalizeJSON(b)
	return errA == nil && errB == nil && bytes.Equal(na, nb)
}

func normalizeJSON(value json.RawMessage) ([]byte, error) {
	var decoded any
	if err := json.Unmarshal(value, &decoded); err != nil {
		return nil, err
	}
	return json.Marshal(decoded)
}

func cloneRaw(value json.RawMessage) json.RawMessage {
	out := make([]byte, len(value))
	copy(out, value)
	return out
}

func rawPtr(value json.RawMessage) *json.RawMessage {
	cloned := cloneRaw(value)
	return &cloned
}

func boolPtr(value bool) *bool {
	return &value
}

type errString string

func (e errString) Error() string { return string(e) }

func sortedPeerIDs(peers map[int]string) []int {
	ids := make([]int, 0, len(peers))
	for id := range peers {
		ids = append(ids, id)
	}
	sort.Ints(ids)
	return ids
}
