package main

import (
	"context"
	"flag"
	"fmt"
	"log"
	"os"
	"os/signal"
	"syscall"
	"time"

	"database/internal/cluster"
)

func main() {
	nodes := flag.Int("nodes", 3, "fixed number of cluster nodes")
	basePort := flag.Int("base-port", 8080, "port for node 0; next nodes use consecutive ports")
	dataDir := flag.String("data", "data", "directory for persisted node logs")
	flag.Parse()

	if *nodes < 1 {
		log.Fatal("nodes must be positive")
	}

	addresses := make(map[int]string, *nodes)
	for i := 0; i < *nodes; i++ {
		addresses[i] = fmt.Sprintf("127.0.0.1:%d", *basePort+i)
	}

	clusterNodes := make([]*cluster.Node, 0, *nodes)
	for id, addr := range addresses {
		peers := make(map[int]string)
		for peerID, peerAddr := range addresses {
			if peerID != id {
				peers[peerID] = peerAddr
			}
		}
		node, err := cluster.NewNode(id, addr, peers, *dataDir)
		if err != nil {
			log.Fatalf("create node %d: %v", id, err)
		}
		clusterNodes = append(clusterNodes, node)
	}

	for _, node := range clusterNodes {
		node.Start()
		log.Printf("node started at %s", node.URL())
	}
	log.Printf("cluster is running; try GET /status on any node")

	stop := make(chan os.Signal, 1)
	signal.Notify(stop, os.Interrupt, syscall.SIGTERM)
	<-stop

	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()
	for _, node := range clusterNodes {
		_ = node.Shutdown(ctx)
	}
}
