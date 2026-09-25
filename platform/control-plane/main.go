package main

import (
	"fmt"
	"log"
	"net/http"
	"os"

	"github.com/ssrjkk/volley/platform/control-plane/internal"
)

func main() {
	repo, err := internal.NewRepositoryFromEnv()
	if err != nil {
		log.Fatalf("failed to initialize repository: %v", err)
	}
	defer repo.Close()

	server := internal.NewServer(repo)
	fmt.Println("volley control plane listening on :8080")
	if err := http.ListenAndServe(":8080", server.Handler()); err != nil {
		log.Fatalf("server failed: %v", err)
	}
	_ = os.Stdout
}
