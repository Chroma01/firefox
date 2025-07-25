// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

#![cfg(not(feature = "disable-encryption"))]
#![cfg(test)]

use neqo_crypto::{
    constants::{TLS_AES_128_GCM_SHA256, TLS_VERSION_1_3},
    init,
    selfencrypt::SelfEncrypt,
    Error,
};

#[test]
fn se_create() {
    init().unwrap();
    SelfEncrypt::new(TLS_VERSION_1_3, TLS_AES_128_GCM_SHA256).expect("constructor works");
}

const PLAINTEXT: &[u8] = b"PLAINTEXT";
const AAD: &[u8] = b"AAD";

fn sealed() -> (SelfEncrypt, Vec<u8>) {
    init().unwrap();
    let se = SelfEncrypt::new(TLS_VERSION_1_3, TLS_AES_128_GCM_SHA256).unwrap();
    let sealed = se.seal(AAD, PLAINTEXT).expect("sealing works");
    (se, sealed)
}

#[test]
fn seal_open() {
    let (se, sealed) = sealed();
    let opened = se.open(AAD, &sealed).expect("opening works");
    assert_eq!(&opened[..], PLAINTEXT);
}

#[test]
fn seal_rotate_open() {
    let (mut se, sealed) = sealed();
    se.rotate().expect("rotate should be infallible");
    let opened = se.open(AAD, &sealed).expect("opening works");
    assert_eq!(&opened[..], PLAINTEXT);
}

#[test]
fn seal_rotate_twice_open() {
    let (mut se, sealed) = sealed();
    se.rotate().expect("rotate should be infallible");
    se.rotate().expect("rotate should be infallible");
    let res = se.open(AAD, &sealed);
    assert_eq!(res.unwrap_err(), Error::SelfEncrypt);
}

#[test]
fn damage_version() {
    let (se, mut sealed) = sealed();
    sealed[0] ^= 0x80;
    let res = se.open(AAD, &sealed);
    assert_eq!(res.unwrap_err(), Error::SelfEncrypt);
}

fn assert_bad_data<T>(res: Result<T, Error>) {
    if let Err(Error::Nss { name, .. }) = res {
        assert_eq!(name, "SEC_ERROR_BAD_DATA");
    }
}

#[test]
fn damage_salt() {
    let (se, mut sealed) = sealed();
    sealed[4] ^= 0x10;
    let res = se.open(AAD, &sealed);
    assert_bad_data(res);
}

#[test]
fn damage_ciphertext() {
    let (se, mut sealed) = sealed();
    sealed[20] ^= 0x2f;
    let res = se.open(AAD, &sealed);
    assert_bad_data(res);
}

#[test]
fn damage_auth_tag() {
    let (se, mut sealed) = sealed();
    let idx = sealed.len() - 1;
    sealed[idx] ^= 0x3;
    let res = se.open(AAD, &sealed);
    assert_bad_data(res);
}

#[test]
fn truncate() {
    let (se, sealed) = sealed();
    let res = se.open(AAD, &sealed[0..(sealed.len() - 1)]);
    assert_bad_data(res);
}
