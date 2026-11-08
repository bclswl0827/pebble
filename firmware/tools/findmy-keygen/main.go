package main

import (
	"errors"
	"flag"
	"fmt"
	"io/fs"
	mathrand "math/rand/v2"
	"os"
	"path/filepath"
	"strings"
)

const (
	prefixLen   = 6
	prefixChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
	maxKeys     = 50
)

func main() {
	if err := run(); err != nil {
		fmt.Fprintln(os.Stderr, "error:", err)
		os.Exit(1)
	}
}

func run() error {
	var (
		nkeys   int
		prefix  string
		yaml    string
		outDir  string
		verbose bool
		agree   string
	)
	flag.IntVar(&nkeys, "n", 1, "")
	flag.IntVar(&nkeys, "nkeys", 1, "")
	flag.StringVar(&prefix, "p", "", "")
	flag.StringVar(&prefix, "prefix", "", "")
	flag.StringVar(&yaml, "y", "", "")
	flag.StringVar(&yaml, "yaml", "", "")
	flag.StringVar(&outDir, "o", "output", "")
	flag.StringVar(&outDir, "output", "output", "")
	flag.BoolVar(&verbose, "v", false, "")
	flag.BoolVar(&verbose, "verbose", false, "")
	flag.StringVar(&agree, "tinfs", "", "")
	flag.StringVar(&agree, "thisisnotforstalking", "", "")
	flag.Usage = usage
	flag.Parse()

	if flag.NArg() > 0 {
		return fmt.Errorf("unexpected argument %q", flag.Arg(0))
	}
	limit := 1
	if agree == "i_agree" {
		limit = maxKeys
	}
	if nkeys < 1 || nkeys > limit {
		return fmt.Errorf("number of keys out of range (between 1 and %d)", limit)
	}
	if prefix == "" {
		prefix = randomPrefix()
	}
	if !validName(prefix) {
		return fmt.Errorf("invalid prefix %q (allowed: A-Z a-z 0-9 _ -)", prefix)
	}
	if yaml != "" && !validName(yaml) {
		return fmt.Errorf("invalid yaml name %q (allowed: A-Z a-z 0-9 _ -)", yaml)
	}

	names := []string{
		prefix + "_keyfile",
		prefix + ".keys",
		prefix + "_devices.json",
		prefix + "_findmy_key.h",
	}
	if yaml != "" {
		names = append(names, prefix+"_"+yaml+".yaml")
	}
	if err := os.MkdirAll(outDir, 0o700); err != nil {
		return err
	}
	deviceDir := filepath.Join(outDir, prefix)
	if err := os.Mkdir(deviceDir, 0o700); errors.Is(err, fs.ErrExist) {
		return fmt.Errorf("%s already exists; refusing to overwrite keys (use another -p or -o)", deviceDir)
	} else if err != nil {
		return err
	}

	fmt.Printf("Output will be written to %s\n", deviceDir)
	keys := make([]keyPair, 0, nkeys)
	for len(keys) < nkeys {
		key, err := generateKey()
		if err != nil {
			return err
		}
		if strings.Contains(key.hashB64[:7], "/") {
			fmt.Println("Key skipped and regenerated, because there was a / in the b64 of the hashed pubkey :(")
			continue
		}
		keys = append(keys, key)
		if verbose {
			fmt.Printf("%d)\n", len(keys))
			fmt.Printf("Private key: %s\n", key.privB64)
			fmt.Printf("Advertisement key: %s\n", key.advB64)
			fmt.Printf("Hashed adv key: %s\n", key.hashB64)
		}
	}

	devices, err := devicesJSON(prefix, keys)
	if err != nil {
		return err
	}
	files := []outputFile{
		{names[0], keyfile(keys)},
		{names[1], []byte(keysText(keys))},
		{names[2], devices},
		{names[3], []byte(cHeader(prefix, keys))},
	}
	if yaml != "" {
		files = append(files, outputFile{names[4], []byte(yamlText(keys))})
	}
	for _, file := range files {
		if err := writeNew(filepath.Join(deviceDir, file.name), file.data); err != nil {
			return err
		}
	}

	leading := keys[len(keys)-1]
	fmt.Println()
	fmt.Printf("Advertisement key (firmware): %s\n", leading.advB64)
	fmt.Printf("Hashed adv key (query ID):    %s\n", leading.hashB64)
	fmt.Printf("BLE MAC (random static):      %s\n", macAddress(leading.adv))
	fmt.Println("Files:")
	for _, file := range files {
		fmt.Printf("  %s\n", filepath.Join(deviceDir, file.name))
	}
	fmt.Printf("Firmware: copy %s to include/findmy_key.h\n", filepath.Join(deviceDir, names[3]))
	fmt.Printf("Reader:   import %s into macless-haystack; keep it private\n", filepath.Join(deviceDir, names[2]))
	return nil
}

func usage() {
	fmt.Fprint(flag.CommandLine.Output(), `Usage: findmy-keygen [options]

Generates OpenHaystack / macless-haystack P-224 keys and the firmware header.

Options:
  -n, --nkeys N       number of keys to generate (default 1)
  -p, --prefix NAME   prefix of the keyfiles (default: random 6 chars)
  -y, --yaml NAME     also write <prefix>_<NAME>.yaml with the advertisement keys
  -o, --output DIR    output root (default "output"); files go to DIR/<prefix>/, never overwritten
  -v, --verbose       print keys as they are generated
`)
}

func randomPrefix() string {
	buf := make([]byte, prefixLen)
	for index := range buf {
		buf[index] = prefixChars[mathrand.IntN(len(prefixChars))]
	}
	return string(buf)
}

func validName(name string) bool {
	if name == "" {
		return false
	}
	for _, char := range name {
		if !(char >= 'A' && char <= 'Z' || char >= 'a' && char <= 'z' ||
			char >= '0' && char <= '9' || char == '_' || char == '-') {
			return false
		}
	}
	return true
}
