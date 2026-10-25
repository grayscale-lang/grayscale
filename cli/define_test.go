package main

import (
	"reflect"
	"testing"
)

func TestExtractDefineFlags(t *testing.T) {
	rest, defines, err := extractDefineFlags([]string{"build", "main.gray", "-define:A=1", "-define:B=x=y", "-o", "app"})
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if want := []string{"build", "main.gray", "-o", "app"}; !reflect.DeepEqual(rest, want) {
		t.Errorf("rest = %v, want %v", rest, want)
	}
	if want := []string{"A=1", "B=x=y"}; !reflect.DeepEqual(defines, want) {
		t.Errorf("defines = %v, want %v", defines, want)
	}
}

func TestExtractDefineFlagsLeavesProgramArguments(t *testing.T) {
	rest, defines, err := extractDefineFlags([]string{"main.gray", "-define:A=1", "--", "-define:B=2"})
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if want := []string{"main.gray", "--", "-define:B=2"}; !reflect.DeepEqual(rest, want) {
		t.Errorf("rest = %v, want %v", rest, want)
	}
	if want := []string{"A=1"}; !reflect.DeepEqual(defines, want) {
		t.Errorf("defines = %v, want %v", defines, want)
	}
}

func TestExtractDefineFlagsRejectsMalformed(t *testing.T) {
	for _, arg := range []string{"-define:A", "-define:", "-define:=1"} {
		if _, _, err := extractDefineFlags([]string{"main.gray", arg}); err == nil {
			t.Errorf("%s: expected an error", arg)
		}
	}
}

func TestExtractDefineFlagsRejectsDuplicate(t *testing.T) {
	if _, _, err := extractDefineFlags([]string{"-define:A=1", "-define:A=2"}); err == nil {
		t.Error("expected an error for a name defined twice")
	}
}
