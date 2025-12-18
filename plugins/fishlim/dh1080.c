/* HexChat
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
*/

/*
 * For Diffie-Hellman key-exchange a 1080bit germain prime is used, the
 * generator g=2 renders a field Fp from 1 to p-1. Therefore breaking it
 * means to solve a discrete logarithm problem with no less than 1080bit.
 *
 * Base64 format is used to send the public keys over IRC.
 *
 * The calculated secret key is hashed with SHA-256, the result is converted
 * to base64 for final use with blowfish.
 */

#include "config.h"
#include "dh1080.h"

#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/core_names.h>
#include <openssl/param_build.h>

#include <string.h>
#include <glib.h>

#define DH1080_PRIME_BITS 1080
#define DH1080_PRIME_BYTES 135
#define SHA256_DIGEST_LENGTH 32
#define B64ABC "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
#define MEMZERO(x) memset(x, 0x00, sizeof(x))

/* All clients must use the same prime number to be able to keyx */
static const guchar prime1080[DH1080_PRIME_BYTES] =
{
	0xFB, 0xE1, 0x02, 0x2E, 0x23, 0xD2, 0x13, 0xE8, 0xAC, 0xFA, 0x9A, 0xE8, 0xB9, 0xDF, 0xAD, 0xA3, 0xEA,
	0x6B, 0x7A, 0xC7, 0xA7, 0xB7, 0xE9, 0x5A, 0xB5, 0xEB, 0x2D, 0xF8, 0x58, 0x92, 0x1F, 0xEA, 0xDE, 0x95,
	0xE6, 0xAC, 0x7B, 0xE7, 0xDE, 0x6A, 0xDB, 0xAB, 0x8A, 0x78, 0x3E, 0x7A, 0xF7, 0xA7, 0xFA, 0x6A, 0x2B,
	0x7B, 0xEB, 0x1E, 0x72, 0xEA, 0xE2, 0xB7, 0x2F, 0x9F, 0xA2, 0xBF, 0xB2, 0xA2, 0xEF, 0xBE, 0xFA, 0xC8,
	0x68, 0xBA, 0xDB, 0x3E, 0x82, 0x8F, 0xA8, 0xBA, 0xDF, 0xAD, 0xA3, 0xE4, 0xCC, 0x1B, 0xE7, 0xE8, 0xAF,
	0xE8, 0x5E, 0x96, 0x98, 0xA7, 0x83, 0xEB, 0x68, 0xFA, 0x07, 0xA7, 0x7A, 0xB6, 0xAD, 0x7B, 0xEB, 0x61,
	0x8A, 0xCF, 0x9C, 0xA2, 0x89, 0x7E, 0xB2, 0x8A, 0x61, 0x89, 0xEF, 0xA0, 0x7A, 0xB9, 0x9A, 0x8A, 0x7F,
	0xA9, 0xAE, 0x29, 0x9E, 0xFA, 0x7B, 0xA6, 0x6D, 0xEA, 0xFE, 0xFB, 0xEF, 0xBF, 0x0B, 0x7D, 0x8B
};

static BIGNUM *g_prime = NULL;
static BIGNUM *g_generator = NULL;

int
dh1080_init (void)
{
	g_return_val_if_fail (g_prime == NULL, 0);

	g_prime = BN_bin2bn (prime1080, DH1080_PRIME_BYTES, NULL);
	g_generator = BN_new ();

	if (g_prime == NULL || g_generator == NULL)
	{
		BN_free (g_prime);
		BN_free (g_generator);
		g_prime = NULL;
		g_generator = NULL;
		return 0;
	}

	BN_set_word (g_generator, 2);
	return 1;
}

void
dh1080_deinit (void)
{
	BN_free (g_prime);
	BN_free (g_generator);
	g_prime = NULL;
	g_generator = NULL;
}

static int
dh1080_verify_pub_key (BIGNUM *pub_key)
{
	/* Public key must be: 1 < pub_key < p-1 */
	BIGNUM *p_minus_1;
	int valid = 0;

	if (BN_is_zero (pub_key) || BN_is_one (pub_key) || BN_is_negative (pub_key))
		return 0;

	p_minus_1 = BN_dup (g_prime);
	if (p_minus_1 == NULL)
		return 0;

	BN_sub_word (p_minus_1, 1);

	/* Check: 1 < pub_key < p-1 */
	if (BN_cmp (pub_key, BN_value_one ()) > 0 && BN_cmp (pub_key, p_minus_1) < 0)
		valid = 1;

	BN_free (p_minus_1);
	return valid;
}

static guchar *
dh1080_decode_b64 (const char *data, gsize *out_len)
{
	GString *str = g_string_new (data);
	guchar *ret;

	if (str->len % 4 == 1 && str->str[str->len - 1] == 'A')
		g_string_truncate (str, str->len - 1);

	while (str->len % 4 != 0)
		g_string_append_c (str, '=');

	ret = g_base64_decode_inplace (str->str, out_len);
	g_string_free (str, FALSE);
  	return ret;
}

static char *
dh1080_encode_b64 (const guchar *data, gsize data_len)
{
	char *ret = g_base64_encode (data, data_len);
	char *p;

	if (!(p = strchr (ret, '=')))
	{
		char *new_ret = g_new(char, strlen(ret) + 2);
		strcpy (new_ret, ret);
		strcat (new_ret, "A");
		g_free (ret);
		ret = new_ret;
	}
	else
	{
		*p = '\0';
	}

  	return ret;
}

/* Create DH parameters EVP_PKEY from our prime and generator */
static EVP_PKEY *
dh1080_create_params (void)
{
	EVP_PKEY *params = NULL;
	EVP_PKEY_CTX *pctx = NULL;
	OSSL_PARAM_BLD *param_bld = NULL;
	OSSL_PARAM *ossl_params = NULL;

	param_bld = OSSL_PARAM_BLD_new ();
	if (param_bld == NULL)
		goto err;

	if (!OSSL_PARAM_BLD_push_BN (param_bld, OSSL_PKEY_PARAM_FFC_P, g_prime) ||
	    !OSSL_PARAM_BLD_push_BN (param_bld, OSSL_PKEY_PARAM_FFC_G, g_generator))
		goto err;

	ossl_params = OSSL_PARAM_BLD_to_param (param_bld);
	if (ossl_params == NULL)
		goto err;

	pctx = EVP_PKEY_CTX_new_from_name (NULL, "DH", NULL);
	if (pctx == NULL)
		goto err;

	if (EVP_PKEY_fromdata_init (pctx) <= 0 ||
	    EVP_PKEY_fromdata (pctx, &params, EVP_PKEY_KEY_PARAMETERS, ossl_params) <= 0)
		goto err;

err:
	OSSL_PARAM_BLD_free (param_bld);
	OSSL_PARAM_free (ossl_params);
	EVP_PKEY_CTX_free (pctx);
	return params;
}

/* Create a full DH key from parameters and existing private/public keys */
static EVP_PKEY *
dh1080_create_key_from_bn (BIGNUM *priv_key, BIGNUM *pub_key)
{
	EVP_PKEY *pkey = NULL;
	EVP_PKEY_CTX *pctx = NULL;
	OSSL_PARAM_BLD *param_bld = NULL;
	OSSL_PARAM *ossl_params = NULL;

	param_bld = OSSL_PARAM_BLD_new ();
	if (param_bld == NULL)
		goto err;

	if (!OSSL_PARAM_BLD_push_BN (param_bld, OSSL_PKEY_PARAM_FFC_P, g_prime) ||
	    !OSSL_PARAM_BLD_push_BN (param_bld, OSSL_PKEY_PARAM_FFC_G, g_generator))
		goto err;

	if (pub_key && !OSSL_PARAM_BLD_push_BN (param_bld, OSSL_PKEY_PARAM_PUB_KEY, pub_key))
		goto err;

	if (priv_key && !OSSL_PARAM_BLD_push_BN (param_bld, OSSL_PKEY_PARAM_PRIV_KEY, priv_key))
		goto err;

	ossl_params = OSSL_PARAM_BLD_to_param (param_bld);
	if (ossl_params == NULL)
		goto err;

	pctx = EVP_PKEY_CTX_new_from_name (NULL, "DH", NULL);
	if (pctx == NULL)
		goto err;

	if (EVP_PKEY_fromdata_init (pctx) <= 0 ||
	    EVP_PKEY_fromdata (pctx, &pkey, EVP_PKEY_KEYPAIR, ossl_params) <= 0)
		goto err;

err:
	OSSL_PARAM_BLD_free (param_bld);
	OSSL_PARAM_free (ossl_params);
	EVP_PKEY_CTX_free (pctx);
	return pkey;
}

int
dh1080_generate_key (char **priv_key, char **pub_key)
{
	guchar buf[DH1080_PRIME_BYTES];
	int len;
	EVP_PKEY *params = NULL;
	EVP_PKEY *pkey = NULL;
	EVP_PKEY_CTX *kctx = NULL;
	BIGNUM *bn_pub = NULL, *bn_priv = NULL;
	int ret = 0;

	g_assert (priv_key != NULL);
	g_assert (pub_key != NULL);

	/* Create DH parameters */
	params = dh1080_create_params ();
	if (params == NULL)
		goto err;

	/* Generate key pair */
	kctx = EVP_PKEY_CTX_new_from_pkey (NULL, params, NULL);
	if (kctx == NULL)
		goto err;

	if (EVP_PKEY_keygen_init (kctx) <= 0)
		goto err;

	if (EVP_PKEY_keygen (kctx, &pkey) <= 0)
		goto err;

	/* Extract public and private keys */
	if (!EVP_PKEY_get_bn_param (pkey, OSSL_PKEY_PARAM_PUB_KEY, &bn_pub) ||
	    !EVP_PKEY_get_bn_param (pkey, OSSL_PKEY_PARAM_PRIV_KEY, &bn_priv))
		goto err;

	/* Encode private key */
	MEMZERO (buf);
	len = BN_bn2bin (bn_priv, buf);
	*priv_key = dh1080_encode_b64 (buf, len);

	/* Encode public key */
	MEMZERO (buf);
	len = BN_bn2bin (bn_pub, buf);
	*pub_key = dh1080_encode_b64 (buf, len);

	OPENSSL_cleanse (buf, sizeof (buf));
	ret = 1;

err:
	BN_free (bn_pub);
	BN_free (bn_priv);
	EVP_PKEY_free (pkey);
	EVP_PKEY_free (params);
	EVP_PKEY_CTX_free (kctx);
	return ret;
}

int
dh1080_compute_key (const char *priv_key, const char *pub_key, char **secret_key)
{
	char *pub_key_data = NULL;
	char *priv_key_data = NULL;
	gsize pub_key_len, priv_key_len;
	BIGNUM *bn_pub = NULL, *bn_priv = NULL, *bn_peer_pub = NULL;
	EVP_PKEY *our_key = NULL, *peer_key = NULL;
	EVP_PKEY_CTX *derive_ctx = NULL;
	guchar *shared_secret = NULL;
	size_t shared_len = 0;
	int ret = 0;

	g_assert (secret_key != NULL);
	*secret_key = NULL;

	/* Verify base64 strings */
	if (strspn (priv_key, B64ABC) != strlen (priv_key) ||
	    strspn (pub_key, B64ABC) != strlen (pub_key))
		return 0;

	/* Decode keys from base64 */
	pub_key_data = (char *)dh1080_decode_b64 (pub_key, &pub_key_len);
	priv_key_data = (char *)dh1080_decode_b64 (priv_key, &priv_key_len);

	bn_peer_pub = BN_bin2bn ((guchar *)pub_key_data, pub_key_len, NULL);
	bn_priv = BN_bin2bn ((guchar *)priv_key_data, priv_key_len, NULL);

	if (bn_peer_pub == NULL || bn_priv == NULL)
		goto err;

	/* Verify peer's public key */
	if (!dh1080_verify_pub_key (bn_peer_pub))
		goto err;

	/* We need to compute our public key from the private key for EVP_PKEY
	 * Our public key = g^priv_key mod p */
	{
		BN_CTX *bn_ctx = BN_CTX_new ();
		if (bn_ctx == NULL)
			goto err;

		bn_pub = BN_new ();
		if (bn_pub == NULL)
		{
			BN_CTX_free (bn_ctx);
			goto err;
		}

		if (!BN_mod_exp (bn_pub, g_generator, bn_priv, g_prime, bn_ctx))
		{
			BN_CTX_free (bn_ctx);
			goto err;
		}
		BN_CTX_free (bn_ctx);
	}

	/* Create our key with private and computed public */
	our_key = dh1080_create_key_from_bn (bn_priv, bn_pub);
	if (our_key == NULL)
		goto err;

	/* Create peer's key with just their public key */
	peer_key = dh1080_create_key_from_bn (NULL, bn_peer_pub);
	if (peer_key == NULL)
		goto err;

	/* Derive shared secret */
	derive_ctx = EVP_PKEY_CTX_new_from_pkey (NULL, our_key, NULL);
	if (derive_ctx == NULL)
		goto err;

	if (EVP_PKEY_derive_init (derive_ctx) <= 0)
		goto err;

	if (EVP_PKEY_derive_set_peer (derive_ctx, peer_key) <= 0)
		goto err;

	/* Get required buffer size */
	if (EVP_PKEY_derive (derive_ctx, NULL, &shared_len) <= 0)
		goto err;

	shared_secret = g_new0 (guchar, shared_len);

	if (EVP_PKEY_derive (derive_ctx, shared_secret, &shared_len) <= 0)
		goto err;

	/* Hash with SHA-256 and encode */
	{
		guchar sha256[SHA256_DIGEST_LENGTH];
		SHA256 (shared_secret, shared_len, sha256);
		*secret_key = dh1080_encode_b64 (sha256, sizeof (sha256));
	}

	ret = 1;

err:
	OPENSSL_cleanse (priv_key_data, priv_key_len);
	g_free (pub_key_data);
	g_free (priv_key_data);
	g_free (shared_secret);
	BN_free (bn_pub);
	BN_free (bn_priv);
	BN_free (bn_peer_pub);
	EVP_PKEY_free (our_key);
	EVP_PKEY_free (peer_key);
	EVP_PKEY_CTX_free (derive_ctx);
	return ret;
}
