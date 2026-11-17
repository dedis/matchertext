// Command autoescape writes the host documents of the HTML replay with Go's html/template, which escapes each value
// for the context in which the template places it: HTML text, an attribute value, a JavaScript string, CSS, or a
// URL. It is the auto-escaping arm of test/matchertext/jssrc/html_driver.mjs.
//
// Input: one JSON string per line, the value. Output: one JSON object per line, from host name to the document of
// that host holding the value. The driver checks that each template writes the document of its host.
package main

import (
	"bufio"
	"encoding/json"
	"fmt"
	"html/template"
	"io"
	"os"
	"strings"
)

// The hosts of html_driver.mjs, without the comment host: html/template drops template comments.
var hosts = map[string]string{
	"text":          `<div id=h>{{.}}</div>`,
	"table_cell":    `<table><tr><td id=h>{{.}}</td></tr></table>`,
	"svg_text":      `<svg><text id=h>{{.}}</text></svg>`,
	"textarea":      `<textarea id=h>{{.}}</textarea>`,
	"title":         `<title id=h>{{.}}</title>`,
	"style":         `<style id=h>{{.}}</style>`,
	"script_string": `<script id=h>var x = "{{.}}";</script>`,
	"attr_dq":       `<div id=h title="{{.}}"></div>`,
	"attr_sq":       `<div id=h title='{{.}}'></div>`,
	"attr_unquoted": `<div id=h title={{.}}></div>`,
	"href":          `<a id=h href="{{.}}"></a>`,
	"onclick":       `<a id=h onclick="{{.}}"></a>`,
	"noscript":      `<body><noscript id=h>{{.}}</noscript>`,
	"option":        `<select><option id=h>{{.}}</option></select>`,
	"template":      `<template id=h>{{.}}</template>`,
}

func main() {
	if err := run(os.Stdin, os.Stdout); err != nil {
		fmt.Fprintf(os.Stderr, "autoescape: %v\n", err)
		os.Exit(1)
	}
}

func run(r io.Reader, w io.Writer) error {
	templates := map[string]*template.Template{}
	for name, text := range hosts {
		t, err := template.New(name).Parse(text)
		if err != nil {
			return err
		}
		templates[name] = t
	}

	in := bufio.NewReader(r)
	out := bufio.NewWriter(w)
	for {
		line, err := in.ReadBytes('\n')
		if err == io.EOF && len(line) == 0 {
			return out.Flush()
		}
		if err != nil && err != io.EOF {
			return err
		}

		var value string
		if err := json.Unmarshal(line, &value); err != nil {
			return fmt.Errorf("malformed input line: %w", err)
		}
		docs := make(map[string]string, len(templates))
		for name, t := range templates {
			var b strings.Builder
			if err := t.Execute(&b, value); err != nil {
				return fmt.Errorf("host %s: %w", name, err)
			}
			docs[name] = b.String()
		}
		encoded, err := json.Marshal(docs)
		if err != nil {
			return err
		}
		out.Write(append(encoded, '\n'))
		// The driver waits for this line before it sends the next value.
		if err := out.Flush(); err != nil {
			return err
		}
	}
}
