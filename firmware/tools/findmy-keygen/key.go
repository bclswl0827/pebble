package main

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/sha256"
	"encoding/base64"
	"fmt"
	"strings"
)

const (
	keyLen = 28
	macLen = 6
)

type keyPair struct {
	adv     []byte
	privB64 string
	advB64  string
	hashB64 string
}

func generateKey() (keyPair, error) {
	priv, err := ecdsa.GenerateKey(elliptic.P224(), rand.Reader)
	if err != nil {
		return keyPair{}, err
	}
	privBytes, err := priv.Bytes()
	if err != nil {
		return keyPair{}, err
	}
	pubBytes, err := priv.PublicKey.Bytes()
	if err != nil {
		return keyPair{}, err
	}
	adv := pubBytes[1 : 1+keyLen]
	hash := sha256.Sum256(adv)
	return keyPair{
		adv:     adv,
		privB64: base64.StdEncoding.EncodeToString(privBytes),
		advB64:  base64.StdEncoding.EncodeToString(adv),
		hashB64: base64.StdEncoding.EncodeToString(hash[:]),
	}, nil
}

// Mirrors user_app_generate_static_addr(): key[0..5] become the random static
// address, most significant byte first, with the top two bits forced to 1.
func macAddress(adv []byte) string {
	parts := make([]string, macLen)
	for index, value := range adv[:macLen] {
		if index == 0 {
			value |= 0xC0
		}
		parts[index] = fmt.Sprintf("%02X", value)
	}
	return strings.Join(parts, ":")
}
