// verify.go — Built-in verification test suite ("gray verify"). Embeds
// a Grayscale test program and runs it through the compiler to confirm
// the installation is working correctly.
//
// Author:  Marshall A Burns (@SchoolyB)
// Copyright (c) 2025-Present Marshall A Burns
// Licensed under the MIT License. See LICENSE for details.

package main

import (
	_ "embed"
	"fmt"
	"os"

	"github.com/grayscale-lang/grayscale/internal/driver"
	"github.com/spf13/cobra"
)

//go:embed tests.gray
var verifyTestSrc []byte

// runVerify writes the embedded tests.gray to a temp file, compiles and runs it
// with grayc, then reports pass/fail. Returns the exit code (0 = pass).
func runVerify() int {
	tmp, err := os.CreateTemp("", "gray-verify-*.gray")
	if err != nil {
		fmt.Fprintf(os.Stderr, "error: could not create temp file: %v\n", err)
		return 1
	}
	defer os.Remove(tmp.Name())

	if _, err := tmp.Write(verifyTestSrc); err != nil {
		tmp.Close()
		fmt.Fprintf(os.Stderr, "error: could not write temp file: %v\n", err)
		return 1
	}
	tmp.Close()

	fmt.Println("Running Grayscale language verification test...")
	code, err := driver.Run(tmp.Name(), nil)
	if err != nil {
		fmt.Fprintf(os.Stderr, "error: %v\n", err)
		return 1
	}
	if code == 0 {
		fmt.Println("Verification passed.")
	} else {
		fmt.Fprintln(os.Stderr, "Verification FAILED.")
	}
	return code
}

var verifyCmd = &cobra.Command{
	Use:   "verify",
	Short: "Run the built-in language verification test suite",
	Args:  cobra.NoArgs,
	RunE: func(cmd *cobra.Command, args []string) error {
		code := runVerify()
		if code != 0 {
			return &ExitError{code}
		}
		return nil
	},
}
