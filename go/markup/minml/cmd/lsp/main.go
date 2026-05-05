package main

import (
	"flag"

	"github.com/dedis/matchertext/go/markup/minml/lsp"
	"github.com/tliron/commonlog"
	_ "github.com/tliron/commonlog/simple"
)

const (
	verbosityNone  = -4
	verbosityDebug = 2
)

func main() {
	addr := flag.String("addr", "", "TCP address to listen on instead of stdio (e.g. :2087)")
	debug := flag.Bool("debug", false, "enable debug logging")
	logPath := flag.String("log", "", "path to log file (default: stderr)")

	// VS Code extension passes -stdio by default. We define it so it doesn't crash.
	// We use the presence of -addr to decide between TCP and Stdio.
	_ = flag.Bool("stdio", true, "use stdio transport (default)")

	flag.Parse()

	// Configure logging
	verbosity := verbosityNone
	if *debug {
		verbosity = verbosityDebug
	}
	var logFilePath *string
	if *logPath != "" {
		logFilePath = logPath
	}
	commonlog.Configure(verbosity, logFilePath)

	s := lsp.NewServer(*debug)

	if *addr != "" {
		s.Server.RunTCP(*addr)
	} else {
		s.Server.RunStdio()
	}
}
