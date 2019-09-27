/* LibTomCrypt, modular cryptographic library -- Tom St Denis
 *
 * LibTomCrypt is a library that provides various cryptographic
 * algorithms in a highly modular and flexible manner.
 *
 * The library is free for all purposes without any express
 * guarantee it works.
 */
#include "tomcrypt_test.h"

/**
  @file ssh_test.c
  Support for SSH data formats (RFC4251), Russ Williams
*/

#ifdef LTC_SSH

#define BUFSIZE 64

/**
  Test vectors from from RFC4251, section 5

  uint32: "the value 699921578 (0x29b7f4aa) is stored as 29 b7 f4 aa"

  string: "the US-ASCII string "testing" is represented as 00 00 00 07 t e s t i n g"

  mpint:
  value (hex)        representation (hex)
  -----------        --------------------
  0                  00 00 00 00
  9a378f9b2e332a7    00 00 00 08 09 a3 78 f9 b2 e3 32 a7
  80                 00 00 00 02 00 80
  -1234              00 00 00 02 ed cc
  -deadbeef          00 00 00 05 ff 21 52 41 11

  name-list:
  value                      representation (hex)
  -----                      --------------------
  (), the empty name-list    00 00 00 00
  ("zlib")                   00 00 00 04 7a 6c 69 62
  ("zlib,none")              00 00 00 09 7a 6c 69 62 2c 6e 6f 6e 65
*/
static const unsigned char  byte1[] = {0x01};
static const unsigned char  byte2[] = {0x71};
static const unsigned char uint32[] = {0x29, 0xb7, 0xf4, 0xaa};
static const unsigned char uint64[] = {0x09, 0xa3, 0x78, 0xf9, 0xb2, 0xe3, 0x32, 0xa7};
static const unsigned char string[] = {0x00, 0x00, 0x00, 0x07, 0x74, 0x65, 0x73, 0x74, 0x69, 0x6e, 0x67};
static const unsigned char mpint1[] = {0x00, 0x00, 0x00, 0x00};
static const unsigned char mpint2[] = {0x00, 0x00, 0x00, 0x08, 0x09, 0xa3, 0x78, 0xf9, 0xb2, 0xe3, 0x32, 0xa7};
static const unsigned char mpint3[] = {0x00, 0x00, 0x00, 0x02, 0x00, 0x80};
static const unsigned char nlist1[] = {0x00, 0x00, 0x00, 0x00};
static const unsigned char nlist2[] = {0x00, 0x00, 0x00, 0x04, 0x7a, 0x6c, 0x69, 0x62};
static const unsigned char nlist3[] = {0x00, 0x00, 0x00, 0x09, 0x7a, 0x6c, 0x69, 0x62, 0x2c, 0x6e, 0x6f, 0x6e, 0x65};


/**
  LTC_SSH encoding test
  @return CRYPT_OK if successful
*/
static int _ssh_encoding_test(void)
{
   unsigned char buffer[BUFSIZE];
   unsigned long buflen;
   void *v, *zero;
   int err;

   /* Buffer too short */
   buflen = 3;
   zeromem(buffer, BUFSIZE);
   SHOULD_FAIL(ssh_encode_sequence_multi(buffer, &buflen,
                                         LTC_SSHDATA_UINT32, 0x29b7f4aa,
                                         LTC_SSHDATA_EOL,    NULL));


   /* byte */
   buflen = BUFSIZE;
   zeromem(buffer, BUFSIZE);
   DO(ssh_encode_sequence_multi(buffer, &buflen,
                                LTC_SSHDATA_BYTE, 0x01,
                                LTC_SSHDATA_EOL,  NULL));
   COMPARE_TESTVECTOR(buffer, buflen, byte1, sizeof(byte1), "enc-byte",    1);

   buflen = BUFSIZE;
   zeromem(buffer, BUFSIZE);
   DO(ssh_encode_sequence_multi(buffer, &buflen,
                                LTC_SSHDATA_BYTE, 0x71,
                                LTC_SSHDATA_EOL,  NULL));
   COMPARE_TESTVECTOR(buffer, buflen, byte2, sizeof(byte2), "enc-byte",    2);
   if (XMEMCMP(buffer, byte2, buflen) != 0) return CRYPT_FAIL_TESTVECTOR;

   /* boolean */
   buflen = BUFSIZE;
   zeromem(buffer, BUFSIZE);
   DO(ssh_encode_sequence_multi(buffer, &buflen,
                                LTC_SSHDATA_BOOLEAN, 0x01,
                                LTC_SSHDATA_EOL,     NULL));
   COMPARE_TESTVECTOR(buffer, buflen, byte1, sizeof(byte1), "enc-boolean", 1);

   buflen = BUFSIZE;
   zeromem(buffer, BUFSIZE);
   DO(ssh_encode_sequence_multi(buffer, &buflen,
                                LTC_SSHDATA_BOOLEAN, 0x71,
                                LTC_SSHDATA_EOL,     NULL));
   /* Should be written out as 0x01 */
   COMPARE_TESTVECTOR(buffer, buflen, byte1, sizeof(byte1), "enc-boolean", 2);

   /* uint32 */
   buflen = BUFSIZE;
   zeromem(buffer, BUFSIZE);
   DO(ssh_encode_sequence_multi(buffer, &buflen,
                                LTC_SSHDATA_UINT32, 0x29b7f4aa,
                                LTC_SSHDATA_EOL,    NULL));
   COMPARE_TESTVECTOR(buffer, buflen, uint32, sizeof(uint32), "enc-uint32", 1);

   /* uint64 */
   buflen = BUFSIZE;
   zeromem(buffer, BUFSIZE);
   DO(ssh_encode_sequence_multi(buffer, &buflen,
                                LTC_SSHDATA_UINT64, CONST64(0x09a378f9b2e332a7),
                                LTC_SSHDATA_EOL,    NULL));
   COMPARE_TESTVECTOR(buffer, buflen, uint64, sizeof(uint64), "enc-uint64", 1);


   /* string */
   buflen = BUFSIZE;
   zeromem(buffer, BUFSIZE);
   DO(ssh_encode_sequence_multi(buffer, &buflen,
                                LTC_SSHDATA_STRING, "testing",
                                LTC_SSHDATA_EOL,    NULL));
   COMPARE_TESTVECTOR(buffer, buflen, string, sizeof(string), "enc-string", 1);


   /* mpint */
   if ((err = mp_init_multi(&zero, &v, NULL)) != CRYPT_OK) {
      return err;
   }

   buflen = BUFSIZE;
   zeromem(buffer, BUFSIZE);
   DO(mp_set(zero, 0));
   DO(ssh_encode_sequence_multi(buffer, &buflen,
                                LTC_SSHDATA_MPINT, zero,
                                LTC_SSHDATA_EOL,   NULL));
   COMPARE_TESTVECTOR(buffer, buflen, mpint1, sizeof(mpint1), "enc-mpint",  1);

   buflen = BUFSIZE;
   zeromem(buffer, BUFSIZE);
   DO(mp_read_radix(v, "9a378f9b2e332a7", 16));
   DO(ssh_encode_sequence_multi(buffer, &buflen,
                                LTC_SSHDATA_MPINT, v,
                                LTC_SSHDATA_EOL,   NULL));
   COMPARE_TESTVECTOR(buffer, buflen, mpint2, sizeof(mpint2), "enc-mpint",  2);

   buflen = BUFSIZE;
   zeromem(buffer, BUFSIZE);
   DO(mp_set(v, 0x80));
   DO(ssh_encode_sequence_multi(buffer, &buflen,
                                LTC_SSHDATA_MPINT, v,
                                LTC_SSHDATA_EOL,   NULL));
   COMPARE_TESTVECTOR(buffer, buflen, mpint3, sizeof(mpint3), "enc-mpint",  3);

   mp_clear_multi(v, zero, NULL);


   /* name-list */
   buflen = BUFSIZE;
   zeromem(buffer, BUFSIZE);
   DO(ssh_encode_sequence_multi(buffer, &buflen,
                                LTC_SSHDATA_NAMELIST, "",
                                LTC_SSHDATA_EOL,      NULL));
   COMPARE_TESTVECTOR(buffer, buflen, nlist1, sizeof(nlist1), "enc-nlist", 1);

   buflen = BUFSIZE;
   zeromem(buffer, BUFSIZE);
   DO(ssh_encode_sequence_multi(buffer, &buflen,
                                LTC_SSHDATA_NAMELIST, "zlib",
                                LTC_SSHDATA_EOL,      NULL));
   COMPARE_TESTVECTOR(buffer, buflen, nlist2, sizeof(nlist2), "enc-nlist", 2);

   buflen = BUFSIZE;
   zeromem(buffer, BUFSIZE);
   DO(ssh_encode_sequence_multi(buffer, &buflen,
                                LTC_SSHDATA_NAMELIST, "zlib,none",
                                LTC_SSHDATA_EOL,      NULL));
   COMPARE_TESTVECTOR(buffer, buflen, nlist3, sizeof(nlist3), "enc-nlist", 3);

   return CRYPT_OK;
}

/**
  LTC_SSH decoding test
  @return CRYPT_OK if successful
*/
static int _ssh_decoding_test(void)
{
   char strbuf[BUFSIZE];
   void *u, *v;
   ulong32 tmp32;
   ulong64 tmp64;
   unsigned char tmp8;
   int err;

   /* byte */
   DO(ssh_decode_sequence_multi(byte1, sizeof(byte1),
                                LTC_SSHDATA_BYTE, &tmp8,
                                LTC_SSHDATA_EOL,  NULL));
   ENSURE(tmp8 == 0x01);

   DO(ssh_decode_sequence_multi(byte2, sizeof(byte2),
                                LTC_SSHDATA_BYTE, &tmp8,
                                LTC_SSHDATA_EOL,  NULL));
   ENSURE(tmp8 == 0x71);

   /* boolean */
   DO(ssh_decode_sequence_multi(byte1, sizeof(byte1),
                                LTC_SSHDATA_BOOLEAN, &tmp8,
                                LTC_SSHDATA_EOL,     NULL));
   ENSURE(tmp8 == 0x01);

   DO(ssh_decode_sequence_multi(byte2, sizeof(byte2),
                                LTC_SSHDATA_BOOLEAN, &tmp8,
                                LTC_SSHDATA_EOL,     NULL));
   ENSURE(tmp8 == 0x01);

   /* uint32 */
   DO(ssh_decode_sequence_multi(uint32, sizeof(uint32),
                                LTC_SSHDATA_UINT32, &tmp32,
                                LTC_SSHDATA_EOL,    NULL));
   ENSURE(tmp32 == 0x29b7f4aa);

   /* uint64 */
   DO(ssh_decode_sequence_multi(uint64, sizeof(uint64),
                                LTC_SSHDATA_UINT64, &tmp64,
                                LTC_SSHDATA_EOL,    NULL));
   if (tmp64 != CONST64(0x09a378f9b2e332a7)) return CRYPT_FAIL_TESTVECTOR;

   /* string */
   zeromem(strbuf, BUFSIZE);
   DO(ssh_decode_sequence_multi(string, sizeof(string),
                                LTC_SSHDATA_STRING, strbuf, BUFSIZE,
                                LTC_SSHDATA_EOL,    NULL));
   ENSURE(XSTRCMP(strbuf, "testing") == 0);

   /* mpint */
   if ((err = mp_init_multi(&u, &v, NULL)) != CRYPT_OK) {
      return err;
   }

   DO(ssh_decode_sequence_multi(mpint1, sizeof(mpint1),
                                LTC_SSHDATA_MPINT, v,
                                LTC_SSHDATA_EOL,   NULL));
   ENSURE(mp_cmp_d(v, 0) == LTC_MP_EQ);

   DO(mp_read_radix(u, "9a378f9b2e332a7", 16));
   DO(ssh_decode_sequence_multi(mpint2, sizeof(mpint2),
                                LTC_SSHDATA_MPINT, v,
                                LTC_SSHDATA_EOL,   NULL));
   ENSURE(mp_cmp(u, v) == LTC_MP_EQ);

   DO(ssh_decode_sequence_multi(mpint3, sizeof(mpint3),
                                LTC_SSHDATA_MPINT, v,
                                LTC_SSHDATA_EOL,   NULL));
   ENSURE(mp_cmp_d(v, 0x80) == LTC_MP_EQ);

   mp_clear_multi(v, u, NULL);

   /* name-list */
   zeromem(strbuf, BUFSIZE);
   DO(ssh_decode_sequence_multi(nlist1, sizeof(nlist1),
                                LTC_SSHDATA_NAMELIST, strbuf, BUFSIZE,
                                LTC_SSHDATA_EOL,      NULL));
   ENSURE(XSTRCMP(strbuf, "") == 0);

   zeromem(strbuf, BUFSIZE);
   DO(ssh_decode_sequence_multi(nlist2, sizeof(nlist2),
                                LTC_SSHDATA_NAMELIST, strbuf, BUFSIZE,
                                LTC_SSHDATA_EOL,      NULL));
   ENSURE(XSTRCMP(strbuf, "zlib") == 0);

   zeromem(strbuf, BUFSIZE);
   DO(ssh_decode_sequence_multi(nlist3, sizeof(nlist3),
                                LTC_SSHDATA_NAMELIST, strbuf, BUFSIZE,
                                LTC_SSHDATA_EOL,      NULL));
   ENSURE(XSTRCMP(strbuf, "zlib,none") == 0);


   return CRYPT_OK;
}

/**
  LTC_SSH self-test
  @return CRYPT_OK if successful, CRYPT_NOP if tests have been disabled.
*/
int ssh_test(void)
{
   if (ltc_mp.name == NULL) return CRYPT_NOP;

   DO(_ssh_encoding_test());
   DO(_ssh_decoding_test());

   return CRYPT_OK;
}

#else

int ssh_test(void)
{
   return CRYPT_NOP;
}

#endif


/* ref:         $Format:%D$ */
/* git commit:  $Format:%H$ */
/* commit time: $Format:%ai$ */
