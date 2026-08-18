package main

import (
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
)

const expectedCommit = "55c241eea321071278d1ee7f7c46292d23e50a5b"

func fail(err error) {
	fmt.Fprintln(os.Stderr, "go control-header oracle:", err)
	os.Exit(1)
}

func main() {
	_, sourceFile, _, ok := runtime.Caller(0)
	if !ok {
		fail(fmt.Errorf("cannot locate runner source"))
	}
	root, err := filepath.Abs(filepath.Join(filepath.Dir(sourceFile), "..", ".."))
	if err != nil {
		fail(err)
	}
	goReference := filepath.Join(root, "third_party", "shmipc-go")

	commitOutput, err := exec.Command("git", "-C", goReference, "rev-parse", "HEAD").Output()
	if err != nil {
		fail(fmt.Errorf("read reference commit: %w", err))
	}
	actualCommit := strings.TrimSpace(string(commitOutput))
	if actualCommit != expectedCommit {
		fail(fmt.Errorf("reference commit mismatch: expected %s, got %s", expectedCommit, actualCommit))
	}

	temporaryDirectory, err := os.MkdirTemp("", "shmipc-go-oracle-")
	if err != nil {
		fail(err)
	}
	defer os.RemoveAll(temporaryDirectory)

	overlay := struct {
		Replace map[string]string
	}{
		Replace: map[string]string{
			filepath.Join(goReference, "zz_control_header_oracle_test.go"): filepath.Join(root, "tools", "go_oracle", "control_header_oracle_test.gotxt"),
		},
	}
	overlayData, err := json.Marshal(overlay)
	if err != nil {
		fail(err)
	}
	overlayPath := filepath.Join(temporaryDirectory, "overlay.json")
	if err := os.WriteFile(overlayPath, overlayData, 0o600); err != nil {
		fail(err)
	}

	command := exec.Command("go", "test", "-overlay", overlayPath, "-run", "^TestControlHeaderGolden$", "-count=1", ".")
	command.Dir = goReference
	command.Env = append(os.Environ(), "SHMIPC_CONTROL_HEADER_GOLDEN="+filepath.Join(root, "tests", "data", "golden", "control_headers.txt"))
	command.Stdout = os.Stdout
	command.Stderr = os.Stderr
	if err := command.Run(); err != nil {
		fail(err)
	}
}
