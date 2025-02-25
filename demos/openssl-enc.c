/* LibTomCrypt, modular cryptographic library -- Tom St Denis */
/* SPDX-License-Identifier: Unlicense */

/*
 * Demo to do the rough equivalent of:
 *
 *    openssl enc -aes-256-cbc -pass pass:foobar -in infile -out outfile -p
 *
 * Compilation:
 *
 *    $(CC) -I /path/to/headers -L .../libs \
 *          -o openssl-enc \
 *          openssl-enc.c -ltomcrypt
 *
 * Usage:
 *
 *    ./openssl-enc <enc|dec> infile outfile "passphrase" [salt]
 *
 * If provided, the salt must be EXACTLY a 16-char hex string.
 *
 * Demo is an example of:
 *
 * - (When decrypting) yanking salt out of the OpenSSL "Salted__..." header
 * - OpenSSL-compatible key derivation (in OpenSSL's modified PKCS#5v1 approach)
 * - Grabbing an Initialization Vector from the key generator
 * - Performing simple block encryption using AES
 *
 * This program is free for all purposes without any express guarantee it
 * works. If you really want to see a license here, assume the WTFPL :-)
 *
 * BJ Black, bblack@barracuda.com, https://wjblack.com
 *
 * BUGS:
 *       Passing a password on a command line is a HORRIBLE idea.  Don't use
 *       this program for serious work!
 */

#include <tomcrypt.h>
#include <termios.h>

#if !defined(LTC_RIJNDAEL) || !defined(LTC_CBC_MODE) || !defined(LTC_PKCS_5) || !defined(LTC_RNG_GET_BYTES) || !defined(LTC_MD5)
int main(void)
{
   return -1;
}
#else

/* OpenSSL by default only runs one hash round */
#define OPENSSL_ITERATIONS 1
/* Use aes-256-cbc, so 256 bits of key, 128 of IV */
#define KEY_LENGTH (256>>3)
#define IV_LENGTH (128>>3)
/* PKCS#5v1 requires exactly an 8-byte salt */
#define SALT_LENGTH 8
/* The header OpenSSL puts on an encrypted file */
static char salt_header[] = { 'S', 'a', 'l', 't', 'e', 'd', '_', '_' };

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* A simple way to handle the possibility that a block may increase in size
   after padding. */
union paddable {
   unsigned char unpad[1024];
   unsigned char pad[1024+MAXBLOCKSIZE];
};

/*
 * Print usage and exit with a bad status (and perror() if any errno).
 *
 * Input:        argv[0] and the error string
 * Output:       <no return>
 * Side Effects: print messages and barf (does exit(3))
 */
static void LTC_NORETURN barf(const char *pname, const char *err)
{
   FILE* o = err == NULL ? stdout : stderr;
   fprintf(o,
            "Usage: %s <enc|dec> infile outfile [passphrase | -] [salt]\n"
            "\n"
            "       The passphrase can either be given at the command line\n"
            "       or if it's passed as '-' it will be read interactively.\n"
            "\n"
            "       # encrypts infile->outfile, random salt\n"
            "       %s enc infile outfile pass\n"
            "\n"
            "       # encrypts infile->outfile, salt from cmdline\n"
            "       %s enc infile outfile pass 0123456789abcdef\n"
            "\n"
            "       # decrypts infile->outfile, pulls salt from infile\n"
            "       %s dec infile outfile pass\n"
            "\n"
            "       # decrypts infile->outfile, salt specified\n"
            "       # (don't try to read the salt from infile)\n"
            "       %s dec infile outfile pass 0123456789abcdef\n"
            "\n"
            "Application Error: %s\n", pname, pname, pname, pname, pname, err ? err : "None");
   if(errno)
      perror(
            "     System Error");
   exit(err == NULL ? 0 : -1);
}

/*
 * Parse the Salted__[+8 bytes] from an OpenSSL-compatible file header.
 *
 * Input:        file to read from and a to put the salt in (exactly 8 bytes!)
 * Output:       CRYPT_OK if parsed OK, CRYPT_ERROR if not
 * Side Effects: infile's read pointer += 16
 */
int parse_openssl_header(FILE *in, unsigned char *out)
{
   unsigned char tmp[SALT_LENGTH];
   if(fread(tmp, 1, sizeof(tmp), in) != sizeof(tmp))
      return CRYPT_ERROR;
   if(memcmp(tmp, salt_header, sizeof(tmp)))
      return CRYPT_ERROR;
   if(fread(tmp, 1, sizeof(tmp), in) != sizeof(tmp))
      return CRYPT_ERROR;
   memcpy(out, tmp, sizeof(tmp));
   return CRYPT_OK;
}

/*
 * Dump a hexed stream of bytes (convenience func).
 *
 * Input:        buf to read from, length
 * Output:       none
 * Side Effects: bytes printed as a hex blob, no lf at the end
 */
void dump_bytes(unsigned char *in, unsigned long len)
{
   unsigned long idx;
   for(idx=0; idx<len; idx++)
      printf("%02hhX", *(in+idx));
}

/*
 * Pad or unpad a message using PKCS#7 padding.
 * Padding will add 1-(blocksize) bytes and unpadding will remove that amount.
 * Set is_padding to 1 to pad, 0 to unpad.
 *
 * Input:        paddable buffer, size read, block length of cipher, mode
 * Output:       number of bytes after padding resp. after unpadding
 * Side Effects: none
 */
static size_t s_pkcs7_pad(union paddable *buf, size_t nb, int block_length,
                 int is_padding)
{
   unsigned long length;

   if(is_padding) {
      length = sizeof(buf->pad);
      if (padding_pad(buf->pad, nb, &length, block_length) != CRYPT_OK)
         return 0;
      return length;
   } else {
      length = nb;
      if (padding_depad(buf->pad, &length, 0) != CRYPT_OK)
         return 0;
      return length;
   }
}

/*
 * Perform an encrypt/decrypt operation to/from files using AES+CBC+PKCS7 pad.
 * Set encrypt to 1 to encrypt, 0 to decrypt.
 *
 * Input:        in/out files, key, iv, and mode
 * Output:       CRYPT_OK if no error
 * Side Effects: bytes slurped from infile, pushed to outfile, fds updated.
 */
int do_crypt(FILE *infd, FILE *outfd, unsigned char *key, unsigned char *iv,
             int encrypt)
{
   union paddable inbuf, outbuf;
   int cipher, ret;
   symmetric_CBC cbc;
   size_t nb;

   /* Register your cipher! */
   cipher = register_cipher(&aes_desc);
   if(cipher == -1)
      return CRYPT_INVALID_CIPHER;

   /* Start a CBC session with cipher/key/val params */
   ret = cbc_start(cipher, iv, key, KEY_LENGTH, 0, &cbc);
   if( ret != CRYPT_OK )
      return -1;

   do {
      /* Get bytes from the source */
      nb = fread(inbuf.unpad, 1, sizeof(inbuf.unpad), infd);
      if(!nb)
         return encrypt ? CRYPT_OK : CRYPT_ERROR;

      /* Barf if we got a read error */
      if(ferror(infd))
         return CRYPT_ERROR;

      if(encrypt) {
         /* We're encrypting, so pad first (if at EOF) and then
            crypt */
         if(feof(infd))
            nb = s_pkcs7_pad(&inbuf, nb,
                           aes_desc.block_length, 1);

         ret = cbc_encrypt(inbuf.pad, outbuf.pad, nb, &cbc);
         if(ret != CRYPT_OK)
            return ret;

      } else {
         /* We're decrypting, so decrypt and then unpad if at
            EOF */
         ret = cbc_decrypt(inbuf.unpad, outbuf.unpad, nb, &cbc);
         if( ret != CRYPT_OK )
            return ret;

         if(feof(infd))
            nb = s_pkcs7_pad(&outbuf, nb,
                           aes_desc.block_length, 0);
         if(nb == 0)
            /* The file didn't decrypt correctly */
            return CRYPT_ERROR;

      }

      /* Push bytes to outfile */
      if(fwrite(outbuf.unpad, 1, nb, outfd) != nb)
         return CRYPT_ERROR;

   } while(!feof(infd));

   /* Close up */
   cbc_done(&cbc);

   return CRYPT_OK;
}


static char* getpassword(const char *prompt, size_t maxlen)
{
   char *wr, *end, *pass = XCALLOC(1, maxlen + 1);
   struct termios tio;
   tcflag_t c_lflag;
   if (pass == NULL)
      return NULL;
   wr = pass;
   end = pass + maxlen;

   tcgetattr(0, &tio);
   c_lflag = tio.c_lflag;
   tio.c_lflag &= ~ECHO;
   tcsetattr(0, TCSANOW, &tio);

   printf("%s", prompt);
   fflush(stdout);
   while (pass < end) {
      int c = getchar();
      if (c == '\r' || c == '\n' || c == -1)
         break;
      *wr++ = c;
   }
   tio.c_lflag = c_lflag;
   tcsetattr(0, TCSAFLUSH, &tio);
   printf("\n");
   return pass;
}

/* Convenience macro for the various barfable places below */
#define BARF(a) { \
   if(password) free(password); \
   if(infd) fclose(infd); \
   if(outfd) { fclose(outfd); remove(argv[3]); } \
   barf(argv[0], a); \
}
/*
 * The main routine.  Mostly validate cmdline params, open files, run the KDF,
 * and do the crypt.
 */
int main(int argc, char *argv[]) {
   unsigned char salt[SALT_LENGTH];
   FILE *infd = NULL, *outfd = NULL;
   int encrypt = -1;
   int hash = -1;
   int ret;
   unsigned char keyiv[KEY_LENGTH + IV_LENGTH];
   unsigned long keyivlen = (KEY_LENGTH + IV_LENGTH);
   unsigned char *key, *iv;
   const void *pass;
   char *password = NULL;
   unsigned long saltlen = sizeof(salt);

   if (argc > 1 && strstr(argv[1], "-h"))
      barf(argv[0], NULL);

   /* Check proper number of cmdline args */
   if(argc < 5 || argc > 6)
      BARF("Invalid number of arguments");

   /* Check proper mode of operation */
   if     (!strncmp(argv[1], "enc", 3))
      encrypt = 1;
   else if(!strncmp(argv[1], "dec", 3))
      encrypt = 0;
   else
      BARF("Bad command name");

   /* Check we can open infile/outfile */
   infd = fopen(argv[2], "rb");
   if(infd == NULL)
      BARF("Could not open infile");
   outfd = fopen(argv[3], "wb");
   if(outfd == NULL)
      BARF("Could not open outfile");

   /* Get the salt from wherever */
   if(argc == 6) {
      /* User-provided */
      if(base16_decode(argv[5], strlen(argv[5]), salt, &saltlen) != CRYPT_OK)
         BARF("Bad user-specified salt");
   } else if(encrypt) {
      /* Encrypting; get from RNG */
      if(rng_get_bytes(salt, sizeof(salt), NULL) != sizeof(salt))
         BARF("Not enough random data");
   } else {
      /* Parse from infile (decrypt only) */
      if(parse_openssl_header(infd, salt) != CRYPT_OK)
         BARF("Invalid OpenSSL header in infile");
   }

   /* Fetch the MD5 hasher for PKCS#5 */
   hash = register_hash(&md5_desc);
   if(hash == -1)
      BARF("Could not register MD5 hash");

   /* Set things to a sane initial state */
   zeromem(keyiv, sizeof(keyiv));
   key = keyiv + 0;      /* key comes first */
   iv = keyiv + KEY_LENGTH;   /* iv comes next */

   if (argv[4] && strcmp(argv[4], "-")) {
      pass = argv[4];
   } else {
      password = getpassword("Enter password: ", 256);
      if (!password)
         BARF("Could not get password");
      pass = password;
   }

   /* Run the key derivation from the provided passphrase.  This gets us
      the key and iv. */
   ret = pkcs_5_alg1_openssl(pass, XSTRLEN(pass), salt,
                             OPENSSL_ITERATIONS, hash, keyiv, &keyivlen );
   if(ret != CRYPT_OK)
      BARF("Could not derive key/iv from passphrase");

   /* Display the salt/key/iv like OpenSSL cmdline does when -p */
   printf("salt="); dump_bytes(salt, sizeof(salt)); printf("\n");
   printf("key=");  dump_bytes(key, KEY_LENGTH);    printf("\n");
   printf("iv =");  dump_bytes(iv,  IV_LENGTH );    printf("\n");

   /* If we're encrypting, write the salt header as OpenSSL does */
   if(!strncmp(argv[1], "enc", 3)) {
      if(fwrite(salt_header, 1, sizeof(salt_header), outfd) !=
         sizeof(salt_header) )
         BARF("Error writing salt header to outfile");
      if(fwrite(salt, 1, sizeof(salt), outfd) != sizeof(salt))
         BARF("Error writing salt to outfile");
   }

   /* At this point, the files are open, the salt has been figured out,
      and we're ready to pump data through crypt. */

   /* Do the crypt operation */
   if(do_crypt(infd, outfd, key, iv, encrypt) != CRYPT_OK)
      BARF("Error during crypt operation");

   /* Clean up */
   if(password) free(password);
   fclose(infd); fclose(outfd);
   return 0;
}
#endif
