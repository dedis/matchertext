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

	// We define the -stdio flag for compatibility with clients that pass it,
	// though the VS Code extension currently uses the presence of -addr
	// to decide between TCP and Stdio transport.
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
