package main

import (
	"encoding/json"
	"fmt"
	mathrand "math/rand/v2"
	"os"
	"strings"
)

type outputFile struct {
	name string
	data []byte
}

type device struct {
	ID              int      `json:"id"`
	ColorComponents []int    `json:"colorComponents"`
	Name            string   `json:"name"`
	PrivateKey      string   `json:"privateKey"`
	Icon            string   `json:"icon"`
	IsActive        bool     `json:"isActive"`
	AdditionalKeys  []string `json:"additionalKeys"`
}

func keyfile(keys []keyPair) []byte {
	data := []byte{byte(len(keys))}
	for _, key := range keys {
		data = append(data, key.adv...)
	}
	return data
}

func keysText(keys []keyPair) string {
	var builder strings.Builder
	for _, key := range keys {
		fmt.Fprintf(&builder, "Private key: %s\n", key.privB64)
		fmt.Fprintf(&builder, "Advertisement key: %s\n", key.advB64)
		fmt.Fprintf(&builder, "Hashed adv key: %s\n", key.hashB64)
	}
	return builder.String()
}

func yamlText(keys []keyPair) string {
	var builder strings.Builder
	builder.WriteString("  keys:\n")
	for _, key := range keys {
		fmt.Fprintf(&builder, "    - \"%s\"\n", key.advB64)
	}
	return builder.String()
}

// The last key is the leading one; the others go to additionalKeys.
func devicesJSON(prefix string, keys []keyPair) ([]byte, error) {
	additional := make([]string, 0, len(keys)-1)
	for _, key := range keys[:len(keys)-1] {
		additional = append(additional, key.privB64)
	}
	data, err := json.MarshalIndent([]device{{
		ID:              mathrand.IntN(10000000),
		ColorComponents: []int{0, 1, 0, 1},
		Name:            prefix,
		PrivateKey:      keys[len(keys)-1].privB64,
		Icon:            "",
		IsActive:        true,
		AdditionalKeys:  additional,
	}}, "", "  ")
	if err != nil {
		return nil, err
	}
	return append(data, '\n'), nil
}

func writeNew(path string, data []byte) error {
	file, err := os.OpenFile(path, os.O_WRONLY|os.O_CREATE|os.O_EXCL, 0o600)
	if err != nil {
		return err
	}
	if _, err := file.Write(data); err != nil {
		file.Close()
		return err
	}
	return file.Close()
}
