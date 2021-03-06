//==================================================================================================
// Project : Teensy HSM
// Author  : Edi Permadi
// Repo    : https://github.com/edipermadi/teensy-hsm
//
// This file is part of TeensyHSM project containing the implementation command dispaching and 
// processing functionality
//==================================================================================================

//--------------------------------------------------------------------------------------------------
// Lookup tables
//--------------------------------------------------------------------------------------------------
static const uint8_t null_nonce[THSM_AEAD_NONCE_SIZE] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

//--------------------------------------------------------------------------------------------------
// GLobal variables
//--------------------------------------------------------------------------------------------------
static FastCRC16       CRC16;
static hmac_sha1_ctx_t hmac_sha1_ctx;

//--------------------------------------------------------------------------------------------------
// Functions
//--------------------------------------------------------------------------------------------------
void execute_cmd()
{
  led_on();

  /* cleanup response and set flag */
  memset(&response, 0, sizeof(response));
  response.cmd  = request.cmd | THSM_FLAG_RESPONSE;

  switch (request.cmd)
  {
    case THSM_CMD_AEAD_GENERATE:
      cmd_aead_generate();
      break;
    case THSM_CMD_BUFFER_AEAD_GENERATE:
      cmd_buffer_aead_generate();
      break;
    case THSM_CMD_RANDOM_AEAD_GENERATE:
      cmd_random_aead_generate();
      break;
    case THSM_CMD_AEAD_DECRYPT_CMP:
      cmd_aead_decrypt_cmp();
      break;
    case THSM_CMD_DB_AEAD_STORE:
      cmd_db_aead_store();
      break;
    case THSM_CMD_AEAD_OTP_DECODE:
      cmd_aead_otp_decode();
      break;
    case THSM_CMD_DB_OTP_VALIDATE:
      cmd_db_otp_validate();
      break;
    case THSM_CMD_DB_AEAD_STORE2:
      cmd_db_aead_store2();
      break;
    case THSM_CMD_AES_ECB_BLOCK_ENCRYPT:
      cmd_ecb_encrypt();
      break;
    case THSM_CMD_AES_ECB_BLOCK_DECRYPT:
      cmd_ecb_decrypt();
      break;
    case THSM_CMD_AES_ECB_BLOCK_DECRYPT_CMP:
      cmd_ecb_decrypt_cmp();
      break;
    case THSM_CMD_HMAC_SHA1_GENERATE:
      cmd_hmac_sha1_generate();
      break;
    case THSM_CMD_TEMP_KEY_LOAD:
      cmd_temp_key_load();
      break;
    case THSM_CMD_BUFFER_LOAD:
      cmd_buffer_load();
      break;
    case THSM_CMD_BUFFER_RANDOM_LOAD:
      cmd_buffer_random_load();
      break;
    case THSM_CMD_NONCE_GET:
      cmd_nonce_get();
      break;
    case THSM_CMD_ECHO:
      cmd_echo();
      break;
    case THSM_CMD_RANDOM_GENERATE:
      cmd_random_generate();
      break;
    case THSM_CMD_RANDOM_RESEED:
      cmd_random_reseed();
      break;
    case THSM_CMD_SYSTEM_INFO_QUERY:
      cmd_info_query();
      break;
    case THSM_CMD_HSM_UNLOCK:
      cmd_hsm_unlock();
      break;
    case THSM_CMD_KEY_STORE_DECRYPT:
      cmd_key_store_decrypt();
      break;
    case THSM_CMD_MONITOR_EXIT:
      break;
  }

  /* send data */
  Serial.write((const char *)&response, (response.bcnt + 1));

  led_off();
}

//--------------------------------------------------------------------------------------------------
// Command Handlers
//--------------------------------------------------------------------------------------------------
static void cmd_echo()
{
  /* cap echo data length to sizeof(THSM_ECHO_REQ::data) */
  uint8_t curr_length = request.payload.echo.data_len;
  uint8_t max_length  = sizeof(request.payload.echo.data);
  uint8_t length      = (curr_length > max_length) ? max_length : curr_length;

  uint8_t *dst_data = response.payload.echo.data;
  uint8_t *src_data = request.payload.echo.data;

  /* copy data */
  memcpy(dst_data, src_data, length);
  response.bcnt = length + 2;
  response.payload.echo.data_len = request.payload.echo.data_len;
}

static void cmd_info_query()
{
  response.bcnt = sizeof(response.payload.system_info) + 1;
  response.payload.system_info.version_major = 1;
  response.payload.system_info.version_minor = 0;
  response.payload.system_info.version_build = 4;
  response.payload.system_info.protocol_version = THSM_PROTOCOL_VERSION;
  memcpy(response.payload.system_info.system_uid, "Teensy HSM  ", THSM_SYSTEM_ID_SIZE);
}

static void cmd_random_generate() {
  uint8_t curr_length = request.payload.random_generate.bytes_len;
  uint8_t max_length  = sizeof(response.payload.random_generate.bytes);
  uint8_t length      = (curr_length > max_length) ? max_length : curr_length;

  response.bcnt = length + 2;
  response.payload.random_generate.bytes_len = length;
  drbg_read(response.payload.random_generate.bytes, length);
}

static void cmd_random_reseed() {
  response.bcnt = 2;
  response.payload.random_reseed.status = THSM_STATUS_OK;

  /* reseed drbg */
  drbg_reseed(request.payload.random_reseed.seed);
}

static void cmd_hmac_sha1_generate() {
  uint8_t  key[THSM_KEY_SIZE];
  uint32_t flags;

  uint8_t *src_key  = request.payload.hmac_sha1_generate.key_handle;
  uint8_t *dst_key  = response.payload.hmac_sha1_generate.key_handle;
  uint8_t *src_data = request.payload.hmac_sha1_generate.data;
  uint8_t *dst_data = response.payload.hmac_sha1_generate.data;

  /* set common response */
  response.bcnt = (sizeof(response.payload.hmac_sha1_generate) - sizeof(response.payload.hmac_sha1_generate.data)) + 1;
  response.payload.hmac_sha1_generate.data_len = 0;
  response.payload.hmac_sha1_generate.status = THSM_STATUS_OK;

  /* copy key handle */
  memcpy(dst_key, src_key, THSM_KEY_HANDLE_SIZE);

  /* check given key handle */
  uint8_t  status     = THSM_STATUS_OK;
  uint16_t length     = request.payload.hmac_sha1_generate.data_len;
  uint32_t key_handle = read_uint32(src_key);
  if (request.bcnt > (sizeof(request.payload.hmac_sha1_generate) + 1)) {
    response.payload.hmac_sha1_generate.status = THSM_STATUS_INVALID_PARAMETER;
  } else if ((length < 1) || (length > sizeof(request.payload.hmac_sha1_generate.data))) {
    response.payload.hmac_sha1_generate.status = THSM_STATUS_INVALID_PARAMETER;
  } else if ((status = keystore_load_key(key, &flags, key_handle)) != THSM_STATUS_OK) {
    response.payload.hmac_sha1_generate.status = status;
  } else {
    /* init hmac */
    uint8_t flags = request.payload.hmac_sha1_generate.flags;
    if (flags & THSM_HMAC_RESET) {
      hmac_sha1_init(&hmac_sha1_ctx, key, sizeof(key));
    }

    /* clear key */
    memset(key, 0, sizeof(key));

    /* update hmac */
    hmac_sha1_update(&hmac_sha1_ctx, src_data, length);

    /* finalize hmac */
    if (flags & THSM_HMAC_FINAL) {
      if (flags & THSM_HMAC_SHA1_TO_BUFFER) {
        hmac_sha1_final(&hmac_sha1_ctx, thsm_buffer.data);
        thsm_buffer.data_len = THSM_SHA1_HASH_SIZE;
      } else {
        hmac_sha1_final(&hmac_sha1_ctx, dst_data);
        response.payload.hmac_sha1_generate.data_len = THSM_SHA1_HASH_SIZE;
        response.bcnt += THSM_SHA1_HASH_SIZE;
      }
    }
  }

  /* clear key */
  memset(key, 0, sizeof(key));
}

static void cmd_ecb_encrypt() {
  uint8_t  key[THSM_KEY_SIZE];
  uint32_t flags;

  /* common response values */
  response.bcnt = sizeof(response.payload.ecb_encrypt) + 1;
  response.payload.ecb_encrypt.status = THSM_STATUS_OK;

  uint8_t *src_key    = request.payload.ecb_encrypt.key_handle;
  uint8_t *dst_key    = response.payload.ecb_encrypt.key_handle;
  uint8_t *plaintext  = request.payload.ecb_encrypt.plaintext;
  uint8_t *ciphertext = response.payload.ecb_encrypt.ciphertext;

  /* copy key handle */
  memcpy(dst_key, src_key, THSM_KEY_HANDLE_SIZE);

  uint8_t  status     = THSM_STATUS_OK;
  uint32_t key_handle = read_uint32(src_key);
  if (request.bcnt != (sizeof(request.payload.ecb_encrypt) + 1)) {
    response.payload.ecb_encrypt.status = THSM_STATUS_INVALID_PARAMETER;
  } else if ((status = keystore_load_key(key, &flags, key_handle)) != THSM_STATUS_OK) {
    response.payload.ecb_encrypt.status = status;
  } else {
    /* perform encryption */
    aes_ecb_encrypt(ciphertext, plaintext, key, sizeof(key));
  }

  /* clear key */
  memset(key, 0, sizeof(key));
}

static void cmd_ecb_decrypt() {
  uint8_t  key[THSM_KEY_SIZE];
  uint32_t flags;

  /* common response values */
  response.bcnt = sizeof(response.payload.ecb_decrypt) + 1;
  response.payload.ecb_decrypt.status = THSM_STATUS_OK;

  uint8_t *src_key    = request.payload.ecb_decrypt.key_handle;
  uint8_t *dst_key    = response.payload.ecb_decrypt.key_handle;
  uint8_t *plaintext  = response.payload.ecb_decrypt.plaintext;
  uint8_t *ciphertext = request.payload.ecb_decrypt.ciphertext;

  /* copy key handle */
  memcpy(dst_key, src_key, THSM_KEY_HANDLE_SIZE);

  uint8_t  status     = THSM_STATUS_OK;
  uint32_t key_handle = read_uint32(src_key);
  if (request.bcnt != (sizeof(request.payload.ecb_decrypt) + 1)) {
    response.payload.ecb_decrypt.status = THSM_STATUS_INVALID_PARAMETER;
  } else if ((status = keystore_load_key(key, &flags, key_handle)) != THSM_STATUS_OK) {
    response.payload.ecb_decrypt.status = status;
  } else {
    /* perform decryption */
    aes_ecb_decrypt(plaintext, ciphertext, key, THSM_KEY_SIZE);
  }

  /* clear key */
  memset(key, 0, sizeof(key));
}

static void cmd_ecb_decrypt_cmp() {
  uint8_t  key[THSM_KEY_SIZE];
  uint32_t flags;

  /* common response values */
  response.bcnt = sizeof(response.payload.ecb_decrypt_cmp) + 1;

  uint8_t *src_key    = request.payload.ecb_decrypt_cmp.key_handle;
  uint8_t *dst_key    = response.payload.ecb_decrypt_cmp.key_handle;
  uint8_t *plaintext  = request.payload.ecb_decrypt_cmp.plaintext;
  uint8_t *ciphertext = request.payload.ecb_decrypt_cmp.ciphertext;

  /* copy key handle */
  memcpy(dst_key, src_key, THSM_KEY_HANDLE_SIZE);

  uint8_t  status     = THSM_STATUS_OK;
  uint32_t key_handle = read_uint32(src_key);
  if (request.bcnt != (sizeof(request.payload.ecb_decrypt_cmp) + 1)) {
    response.payload.ecb_decrypt_cmp.status = THSM_STATUS_INVALID_PARAMETER;
  } else if ((status = keystore_load_key(key, &flags, key_handle)) != THSM_STATUS_OK) {
    response.payload.ecb_decrypt_cmp.status = status;
  } else {

    /* perform decryption */
    uint8_t recovered[THSM_BLOCK_SIZE];
    aes_ecb_decrypt(recovered, ciphertext, key, sizeof(key));

    /* compare plaintext */
    uint8_t matched = memcmp(recovered, plaintext, THSM_BLOCK_SIZE);
    response.payload.ecb_decrypt_cmp.status = matched ? THSM_STATUS_MISMATCH : THSM_STATUS_OK;

    /* clear temporary variables */
    memset(recovered, 0, sizeof(recovered));
  }

  /* clear key */
  memset(key, 0, sizeof(key));
}

static void cmd_buffer_load() {
  /* limit offset */
  uint8_t max_offset  = sizeof(request.payload.buffer_load.data) - 1;
  uint8_t curr_offset = request.payload.buffer_load.offset;
  uint8_t offset      = (curr_offset > max_offset) ? max_offset : curr_offset;

  /* offset + length must be sizeof(request.payload.buffer_load.data) */
  uint8_t max_length  = sizeof(request.payload.buffer_load.data) - offset;
  uint8_t curr_length = request.payload.buffer_load.data_len;
  uint8_t length      = (curr_length > max_length) ? max_length : curr_length;

  /* set request length */
  request.bcnt = request.payload.buffer_load.data_len + 3;

  /* copy data to buffer */
  uint8_t *src_data = request.payload.buffer_load.data;
  memcpy(&thsm_buffer.data[offset], src_data, length);
  thsm_buffer.data_len = (offset > 0) ? (thsm_buffer.data_len + length) : length;

  /* prepare response */
  response.bcnt = sizeof(response.payload.buffer_load) + 1;
  response.payload.buffer_load.length = thsm_buffer.data_len;
}

static void cmd_buffer_random_load() {
  /* limit offset */
  uint8_t max_offset  = sizeof(thsm_buffer.data) - 1;
  uint8_t curr_offset = request.payload.buffer_random_load.offset;
  uint8_t offset      = (curr_offset > max_offset) ? max_offset : curr_offset;

  /* offset + length must be sizeof(thsm_buffer.data) */
  uint8_t max_length  = sizeof(thsm_buffer.data)  - offset;
  uint8_t curr_length = request.payload.buffer_random_load.length;
  uint8_t length      = (curr_length > max_length) ? max_length : curr_length;

  /* fill buffer with random */
  drbg_read(&thsm_buffer.data[offset], length);
  thsm_buffer.data_len = (offset > 0) ? (thsm_buffer.data_len + length) : length;

  /* prepare response */
  response.bcnt = sizeof(response.payload.buffer_random_load) + 1;
  response.payload.buffer_random_load.length = thsm_buffer.data_len;
}

static void cmd_hsm_unlock() {
  uint8_t secret[THSM_KEY_SIZE + THSM_AEAD_NONCE_SIZE];
  /* prepare response */
  response.bcnt = sizeof(response.payload.hsm_unlock) + 1;

  uint8_t *public_id = request.payload.hsm_unlock.public_id;
  uint8_t *otp       = request.payload.hsm_unlock.otp;
  uint8_t status     = THSM_STATUS_OK;

  /* check request byte count */
  if (request.bcnt != (sizeof(request.payload.hsm_unlock) + 1)) {
    response.payload.hsm_unlock.status = THSM_STATUS_INVALID_PARAMETER;
  } else if ((status = keystore_load_secret(secret, public_id)) != THSM_STATUS_OK) {
    response.payload.hsm_unlock.status = status;
  } else {
    uint8_t decoded[THSM_BLOCK_SIZE];
    uint8_t *key       = secret;
    uint8_t *uid_ref   = secret + THSM_KEY_SIZE;
    uint8_t *uid_act   = decoded + 2;
    uint8_t *counter   = uid_act + THSM_AEAD_NONCE_SIZE;
    uint8_t length     = THSM_KEY_SIZE - sizeof(uint16_t);

    /* decrypt otp */
    aes_ecb_decrypt(decoded, otp, key, THSM_KEY_SIZE);

    /* lock secret by default */
    secret_locked(true);

    /* compare CRC16 */
    uint16_t crc = (decoded[0] << 8) | decoded[1];
    if (crc != CRC16.ccitt(uid_act, length)) {
      response.payload.hsm_unlock.status = THSM_STATUS_OTP_INVALID;
    } else if (!memcmp(uid_ref, uid_act, THSM_AEAD_NONCE_SIZE)) {
      response.payload.hsm_unlock.status = THSM_STATUS_OTP_INVALID;
    } else if ((status = keystore_check_counter(public_id, counter)) != THSM_STATUS_OK) {
      response.payload.hsm_unlock.status = status;
    } else {
      secret_locked(false);

      /* save updated flash cache */
      keystore_update();
    }

    /* clear temporary variable */
    memset(decoded, 0, sizeof(decoded));
  }

  /* clear secret */
  memset(secret, 0, sizeof(secret));
}

static void cmd_key_store_decrypt() {
  /* prepare response */
  response.bcnt = sizeof(response.payload.key_store_decrypt) + 1;

  /* check request byte count */
  if (request.bcnt != (sizeof(request.payload.key_store_decrypt) + 1)) {
    response.payload.key_store_decrypt.status = THSM_STATUS_INVALID_PARAMETER;
  } else {
    /* unlock keystore */
    uint8_t *key   = request.payload.key_store_decrypt.key;
    uint8_t status = keystore_unlock(key);
    response.payload.key_store_decrypt.status = status;
  }
}

static void cmd_nonce_get() {
  /* prepare response */
  response.bcnt = sizeof(response.payload.nonce_get) + 1;
  response.payload.nonce_get.status = THSM_STATUS_OK;

  if (request.bcnt != (sizeof(request.payload.nonce_get) + 1)) {
    response.payload.nonce_get.status = THSM_STATUS_INVALID_PARAMETER;
  } else {
    uint16_t step = (request.payload.nonce_get.post_inc[0] << 8) | request.payload.nonce_get.post_inc[1];
    nonce_pool_read(response.payload.nonce_get.nonce, step);
  }
}

static void cmd_aead_generate() {
  uint8_t  key[THSM_KEY_SIZE];
  uint32_t flags;

  /* prepare response */
  response.bcnt = (sizeof(response.payload.aead_generate) - sizeof(response.payload.aead_generate.data)) + 1;
  response.payload.aead_generate.status = THSM_STATUS_OK;

  uint8_t *src_nonce = request.payload.aead_generate.nonce;
  uint8_t *dst_nonce = response.payload.aead_generate.nonce;
  uint8_t *src_key   = request.payload.aead_generate.key_handle;
  uint8_t *dst_key   = response.payload.aead_generate.key_handle;
  uint8_t *src_data  = request.payload.aead_generate.data;
  uint8_t *dst_data  = response.payload.aead_generate.data;

  /* copy key handle and nonce */
  memcpy(dst_key,   src_key,   THSM_KEY_HANDLE_SIZE);
  memcpy(dst_nonce, src_nonce, THSM_AEAD_NONCE_SIZE);
  uint8_t min_length = sizeof(request.payload.aead_generate) - sizeof(request.payload.aead_generate.data);

  /* get key handle, status and length */
  uint32_t key_handle = read_uint32(src_key);
  uint8_t  status     = THSM_STATUS_OK;
  uint16_t length     = request.payload.aead_generate.data_len;

  /* check parameters */
  if (request.bcnt != (min_length + request.payload.aead_generate.data_len + 1)) {
    response.payload.aead_generate.status = THSM_STATUS_INVALID_PARAMETER;
  } else if ((length < 1) || (length > sizeof(request.payload.aead_generate.data))) {
    response.payload.aead_generate.status = THSM_STATUS_INVALID_PARAMETER;
  } else if ((status = keystore_load_key(key, &flags, key_handle)) != THSM_STATUS_OK) {
    response.payload.aead_generate.status = status;
  } else {

    /* generate nonce */
    if (!memcmp(dst_nonce, null_nonce, THSM_AEAD_NONCE_SIZE)) {
      nonce_pool_read(dst_nonce, 1);
    }

    /* perform encryption */
    aes128_ccm_encrypt(dst_data, NULL, src_data, length, dst_key, key, dst_nonce);

    /* set response */
    response.payload.aead_generate.data_len = length + THSM_AEAD_MAC_SIZE;
    response.bcnt += response.payload.aead_generate.data_len;
  }

  /* clear key */
  memset(key, 0, sizeof(key));
}

static void cmd_buffer_aead_generate() {
  uint8_t  key[THSM_KEY_SIZE];
  uint32_t flags;

  /* prepare response */
  response.bcnt = (sizeof(response.payload.buffer_aead_generate) - sizeof(response.payload.buffer_aead_generate.data)) + 1;
  response.payload.buffer_aead_generate.status = THSM_STATUS_OK;

  uint8_t *src_nonce = request.payload.buffer_aead_generate.nonce;
  uint8_t *dst_nonce = response.payload.buffer_aead_generate.nonce;
  uint8_t *src_key   = request.payload.buffer_aead_generate.key_handle;
  uint8_t *dst_key   = response.payload.buffer_aead_generate.key_handle;
  uint8_t *dst_data  = response.payload.buffer_aead_generate.data;

  /* copy key handle and nonce */
  memcpy(dst_key,   src_key,   THSM_KEY_HANDLE_SIZE);
  memcpy(dst_nonce, src_nonce, THSM_AEAD_NONCE_SIZE);

  /* get key handle, status and length */
  uint32_t key_handle = read_uint32(src_key);
  uint8_t  status     = THSM_STATUS_OK;
  uint16_t length     = thsm_buffer.data_len;

  /* check parameters */
  if (request.bcnt != (sizeof(request.payload.buffer_aead_generate) + 1)) {
    response.payload.buffer_aead_generate.status = THSM_STATUS_INVALID_PARAMETER;
  } else if (length < 1) {
    response.payload.buffer_aead_generate.status = THSM_STATUS_INVALID_PARAMETER;
  } else if ((status = keystore_load_key(key, &flags, key_handle)) != THSM_STATUS_OK) {
    response.payload.buffer_aead_generate.status = status;
  } else {
    /* generate nonce */
    if (!memcmp(dst_nonce, null_nonce, THSM_AEAD_NONCE_SIZE)) {
      nonce_pool_read(dst_nonce, 1);
    }

    /* perform CCM encryption */
    aes128_ccm_encrypt(dst_data, NULL, thsm_buffer.data, length, dst_key, key, dst_nonce);

    /* set response */
    response.payload.buffer_aead_generate.data_len = (length + THSM_AEAD_MAC_SIZE);
    response.bcnt += response.payload.buffer_aead_generate.data_len;
  }

  /* clear key */
  memset(key, 0, sizeof(key));
}

static void cmd_random_aead_generate() {
  uint8_t  key[THSM_KEY_SIZE];
  uint32_t flags;

  /* prepare response */
  response.bcnt = (sizeof(response.payload.random_aead_generate) - sizeof(response.payload.random_aead_generate.data)) + 1;
  response.payload.random_aead_generate.status = THSM_STATUS_OK;

  uint8_t *src_nonce = request.payload.random_aead_generate.nonce;
  uint8_t *dst_nonce = response.payload.random_aead_generate.nonce;
  uint8_t *src_key   = request.payload.random_aead_generate.key_handle;
  uint8_t *dst_key   = response.payload.random_aead_generate.key_handle;
  uint8_t *dst_data  = response.payload.random_aead_generate.data;

  /* copy key handle and nonce */
  memcpy(dst_key,   src_key,   THSM_KEY_HANDLE_SIZE);
  memcpy(dst_nonce, src_nonce, THSM_AEAD_NONCE_SIZE);

  /* get key handle, status and length */
  uint32_t key_handle = read_uint32(src_key);
  uint8_t  status     = THSM_STATUS_OK;
  uint16_t length     = request.payload.random_aead_generate.random_len;

  /* check parameters */
  if (request.bcnt != (sizeof(request.payload.random_aead_generate) + 1)) {
    response.payload.random_aead_generate.status = THSM_STATUS_INVALID_PARAMETER;
  } else if ((length < 1) || (length > THSM_DATA_BUF_SIZE)) {
    response.payload.random_aead_generate.status = THSM_STATUS_INVALID_PARAMETER;
  } else if ((status = keystore_load_key(key, &flags, key_handle)) != THSM_STATUS_OK) {
    response.payload.random_aead_generate.status = status;
  } else {
    /* generate nonce */
    if (!memcmp(dst_nonce, null_nonce, THSM_AEAD_NONCE_SIZE)) {
      nonce_pool_read(dst_nonce, 1);
    }

    /* genarate random */
    uint8_t random_buffer[THSM_DATA_BUF_SIZE];
    drbg_read(random_buffer, length);

    /* perform AES CCM encryption */
    aes128_ccm_encrypt(dst_data, NULL, random_buffer, length, dst_key, key, dst_nonce);

    /* set response */
    response.payload.random_aead_generate.data_len = length + THSM_AEAD_MAC_SIZE;
    response.bcnt += (length + THSM_AEAD_MAC_SIZE);

    /* clear random buffer */
    memset(random_buffer, 0, sizeof(random_buffer));
  }

  /* clear key */
  memset(key, 0, sizeof(key));
}

static void cmd_aead_decrypt_cmp() {
  uint8_t  key[THSM_KEY_SIZE];
  uint32_t flags;

  /* prepare response */
  response.bcnt = sizeof(response.payload.aead_decrypt_cmp) + 1;

  uint8_t *src_nonce = request.payload.aead_decrypt_cmp.nonce;
  uint8_t *dst_nonce = response.payload.aead_decrypt_cmp.nonce;
  uint8_t *src_key   = request.payload.aead_decrypt_cmp.key_handle;
  uint8_t *dst_key   = response.payload.aead_decrypt_cmp.key_handle;
  uint8_t *src_data  = request.payload.aead_decrypt_cmp.data;

  /* copy key handle and nonce */
  memcpy(dst_key,   src_key, THSM_KEY_HANDLE_SIZE);
  memcpy(dst_nonce, src_nonce, THSM_AEAD_NONCE_SIZE);
  uint8_t min_length = sizeof(request.payload.aead_decrypt_cmp) - sizeof(request.payload.aead_decrypt_cmp.data);

  /* get key handle */
  uint32_t key_handle  = read_uint32(src_key);
  uint8_t  status      = THSM_STATUS_OK;
  uint16_t data_length = request.payload.aead_decrypt_cmp.data_len;

  /* check parameters */
  if (request.bcnt != (min_length + request.payload.aead_decrypt_cmp.data_len + 1)) {
    response.payload.aead_decrypt_cmp.status = THSM_STATUS_INVALID_PARAMETER;
  } else if ((data_length < 8) || (data_length > 72) || (data_length & 0x01)) {
    response.payload.aead_decrypt_cmp.status = THSM_STATUS_KEY_HANDLE_INVALID;
  } else if ((status = keystore_load_key(key, &flags, key_handle)) != THSM_STATUS_OK) {
    response.payload.aead_decrypt_cmp.status = status;
  } else {
    /* calculate length */
    uint8_t length = (data_length - THSM_AEAD_MAC_SIZE) >> 1;

    uint8_t recovered[32];
    uint8_t *plaintext  = src_data;
    uint8_t *ciphertext = plaintext  + length;
    uint8_t *mac        = ciphertext + length;

    /* initialize */
    memset(recovered,  0, sizeof(recovered));

    /* generate nonce */
    if (!memcmp(dst_nonce, null_nonce, THSM_AEAD_NONCE_SIZE)) {
      nonce_pool_read(dst_nonce, 1);
    }

    /* perform AES CCM decryption */
    uint8_t mac_matched = aes128_ccm_decrypt(recovered, ciphertext, length, dst_key, key, dst_nonce, mac);
    uint8_t pt_matched  = !memcmp(recovered, plaintext, length);

    /* set response */
    response.payload.aead_decrypt_cmp.status = (mac_matched && pt_matched) ? THSM_STATUS_OK : THSM_STATUS_MISMATCH;

    /* clear temporary variables */
    memset(recovered,  0, sizeof(recovered));
  }

  /* clear key */
  memset(key, 0, sizeof(key));
}

static void cmd_temp_key_load() {
  uint8_t  key[THSM_KEY_SIZE];
  uint32_t flags;

  /* prepare response */
  response.bcnt = sizeof(response.payload.temp_key_load) + 1;
  response.payload.temp_key_load.status = THSM_STATUS_OK;

  uint8_t *src_key   = request.payload.temp_key_load.key_handle;
  uint8_t *dst_key   = response.payload.temp_key_load.key_handle;
  uint8_t *src_nonce = request.payload.temp_key_load.nonce;
  uint8_t *dst_nonce = response.payload.temp_key_load.nonce;
  uint8_t *src_data  = request.payload.temp_key_load.data;

  /* copy key handle and nonce */
  memcpy(dst_key, src_key, THSM_KEY_HANDLE_SIZE);
  memcpy(dst_nonce, src_nonce, THSM_AEAD_NONCE_SIZE);

  uint32_t key_handle = read_uint32(src_key);
  uint8_t  status     = THSM_STATUS_OK;
  uint16_t data_len   = request.payload.temp_key_load.data_len;
  if (request.bcnt != sizeof(request.payload.temp_key_load) + 1) {
    response.payload.temp_key_load.status = THSM_STATUS_INVALID_PARAMETER;
  } else if ((data_len != 12) || (data_len != 28) || (data_len != 32) || (data_len != 36) || (data_len != 44)) {
    response.payload.hmac_sha1_generate.status = THSM_STATUS_INVALID_PARAMETER;
  } else if ((status = keystore_load_key(key, &flags, key_handle)) != THSM_STATUS_OK) {
    response.payload.hmac_sha1_generate.status = status;
  } else {

    /* clear temporary key and quit */
    if (data_len == 12) {
      keystore_store_key(0xffffffff, 0, NULL);
      return;
    }

    /* generate nonce */
    if (!memcmp(dst_nonce, null_nonce, THSM_AEAD_NONCE_SIZE)) {
      nonce_pool_read(dst_nonce, 1);
    }

    uint8_t length = data_len - (THSM_KEY_HANDLE_SIZE + THSM_AEAD_MAC_SIZE);
    uint8_t ciphertext[32];
    uint8_t plaintext[32];
    uint8_t mac[THSM_AEAD_MAC_SIZE];
    uint8_t flags[THSM_KEY_FLAGS_SIZE];

    /* initialize */
    memset(ciphertext, 0, sizeof(ciphertext));
    memset(plaintext,  0, sizeof(plaintext));

    /* load mac and ciphertext */
    memcpy(ciphertext, src_data, length);
    memcpy(mac,        src_data + length, THSM_AEAD_MAC_SIZE);
    memcpy(flags,      src_data + length + THSM_AEAD_MAC_SIZE, THSM_KEY_FLAGS_SIZE);

    /* perform AES CCM decryption */
    uint8_t matched = aes128_ccm_decrypt(plaintext, ciphertext, length, dst_key, key, dst_nonce, mac);

    /* Copy to temporary key */
    if (matched) {
      keystore_store_key(0xffffffff, 0, plaintext);
    }

    /* set response */
    response.payload.temp_key_load.status = matched ? THSM_STATUS_OK : THSM_STATUS_MISMATCH;

    /* clear temporary variables */
    memset(ciphertext, 0, sizeof(ciphertext));
    memset(plaintext,  0, sizeof(plaintext));
    memset(mac,        0, sizeof(mac));
    memset(flags,      0, sizeof(flags));
  }

  /* clear key */
  memset(key, 0, sizeof(key));
}

static void cmd_db_aead_store() {
  uint8_t  key[THSM_KEY_SIZE];
  uint32_t flags;

  uint8_t *src_key   = request.payload.db_aead_store.key_handle;
  uint8_t *dst_key   = response.payload.db_aead_store.key_handle;
  uint8_t *src_data  = request.payload.db_aead_store.aead;
  uint8_t *src_pub   = request.payload.db_aead_store.public_id;
  uint8_t *dst_pub   = response.payload.db_aead_store.public_id;

  /* copy key handle and public id */
  memcpy(dst_key, src_key, THSM_KEY_HANDLE_SIZE);
  memcpy(dst_pub, src_pub, THSM_PUBLIC_ID_SIZE);

  /* get key handle */
  uint32_t key_handle = read_uint32(src_key);
  uint8_t  status     = THSM_STATUS_OK;

  /* load key */
  if (request.bcnt > (sizeof(request.payload.db_aead_store) + 1)) {
    response.payload.db_aead_store.status = THSM_STATUS_INVALID_PARAMETER;
  } else if ((status = keystore_load_key(key, &flags, key_handle)) != THSM_STATUS_OK) {
    response.payload.db_aead_store.status = status;
  } else {
    uint8_t length      = (THSM_KEY_SIZE + THSM_AEAD_NONCE_SIZE);
    uint8_t *ciphertext = src_data;
    uint8_t *mac        = src_data + length;
    uint8_t recovered[THSM_KEY_SIZE + THSM_AEAD_NONCE_SIZE];

    /* clear buffer */
    memset(recovered, 0, sizeof(recovered));

    /* perform AES CCM decryption */
    uint8_t matched = aes128_ccm_decrypt(recovered, ciphertext, length, dst_key, key, src_pub, mac);
    if (matched) {
      status = keystore_store_secret(src_pub, recovered);
      response.payload.db_aead_store.status = status;

      /* save updated flash cache */
      if (status == THSM_STATUS_OK) {
        keystore_update();
      }
    } else {
      response.payload.db_aead_store.status = THSM_STATUS_AEAD_INVALID;
    }

    /* clear recovered */
    memset(recovered, 0, sizeof(recovered));
  }

  /* clear key */
  memset(key, 0, sizeof(key));
}

static void cmd_db_aead_store2() {
  uint8_t  key[THSM_KEY_SIZE];
  uint32_t flags;

  uint8_t *src_key   = request.payload.db_aead_store2.key_handle;
  uint8_t *dst_key   = response.payload.db_aead_store2.key_handle;
  uint8_t *src_data  = request.payload.db_aead_store2.aead;
  uint8_t *src_pub   = request.payload.db_aead_store2.public_id;
  uint8_t *dst_pub   = response.payload.db_aead_store2.public_id;
  uint8_t *src_nonce = request.payload.db_aead_store2.nonce;

  /* copy key handle and public id */
  memcpy(dst_key, src_key, THSM_KEY_HANDLE_SIZE);
  memcpy(dst_pub, src_pub, THSM_PUBLIC_ID_SIZE);

  /* read key handle */
  uint32_t key_handle = read_uint32(src_key);
  uint8_t  status     = THSM_STATUS_OK;

  /* load key */
  if (request.bcnt > (sizeof(request.payload.db_aead_store) + 1)) {
    response.payload.db_aead_store2.status = THSM_STATUS_INVALID_PARAMETER;
  } else if ((status = keystore_load_key(key, &flags, key_handle)) != THSM_STATUS_OK) {
    response.payload.db_aead_store2.status = status;
  } else {
    uint8_t length      = (THSM_KEY_SIZE + THSM_AEAD_NONCE_SIZE);
    uint8_t *ciphertext = src_data;
    uint8_t *mac        = src_data + length;
    uint8_t recovered[THSM_KEY_SIZE + THSM_PUBLIC_ID_SIZE];

    /* init buffer */
    memset(recovered, 0, sizeof(recovered));

    /* perform AES CCM decryption */
    uint8_t matched = aes128_ccm_decrypt(recovered, ciphertext, length, dst_key, key, src_nonce, mac);
    if (matched) {
      uint8_t status = keystore_store_secret(src_pub, recovered);
      response.payload.db_aead_store.status = status;

      /* save updated flash cache */
      if (status == THSM_STATUS_OK) {
        keystore_update();
      }
    } else {
      response.payload.db_aead_store.status = THSM_STATUS_AEAD_INVALID;
    }

    /* clear recovered */
    memset(recovered, 0, sizeof(recovered));
  }

  /* clear key */
  memset(key, 0, sizeof(key));
}

static void cmd_aead_otp_decode() {
  uint32_t flags = 0;
  uint8_t key[THSM_KEY_SIZE];

  uint8_t *src_key  = request.payload.aead_otp_decode.key_handle;
  uint8_t *dst_key  = response.payload.aead_otp_decode.key_handle;
  uint8_t *src_pub  = request.payload.aead_otp_decode.public_id;
  uint8_t *dst_pub  = response.payload.aead_otp_decode.public_id;
  uint8_t *src_otp  = request.payload.aead_otp_decode.otp;
  uint8_t *src_aead = request.payload.aead_otp_decode.aead;


  /* copy key handle, public id */
  memcpy(dst_key, src_key, THSM_KEY_HANDLE_SIZE);
  memcpy(dst_pub, src_pub, THSM_PUBLIC_ID_SIZE);

  /* get key handle */
  uint8_t status      = THSM_STATUS_OK;
  uint32_t key_handle = read_uint32(src_key);

  /* check parameters */
  if (request.bcnt > (sizeof(request.payload.aead_otp_decode) + 1)) {
    response.payload.aead_otp_decode.status = THSM_STATUS_INVALID_PARAMETER;
  } else if ((status = keystore_load_key(key, &flags, key_handle)) != THSM_STATUS_OK) {
    response.payload.aead_otp_decode.status = status;
  } else {
    uint8_t recovered[THSM_KEY_SIZE + THSM_PUBLIC_ID_SIZE];
    uint8_t length      = (THSM_KEY_SIZE + THSM_PUBLIC_ID_SIZE);
    uint8_t *ciphertext = src_aead;
    uint8_t *mac        = src_aead + length;
    uint8_t *public_id  = recovered + THSM_KEY_SIZE;

    /* init buffer */
    memset(recovered, 0, sizeof(recovered));

    /* perform AES CCM decryption */
    uint8_t matched = aes128_ccm_decrypt(recovered, ciphertext, length, dst_key, key, src_pub, mac);
    if (matched) {
      uint8_t decoded[THSM_BLOCK_SIZE]; // CRC16 || uid || counter || rand

      /* perform AES ECB decryption */
      aes_ecb_decrypt(decoded, src_otp, recovered, THSM_KEY_SIZE);

      /* compare CRC-16 */
      uint16_t crc = (decoded[0] << 8) | decoded[1];
      if (crc != CRC16.ccitt(decoded + 2, 14)) {
        response.payload.aead_otp_decode.status = THSM_STATUS_OTP_INVALID;
      } else if (memcmp(public_id, decoded + 2, THSM_PUBLIC_ID_SIZE)) {
        response.payload.aead_otp_decode.status = THSM_STATUS_OTP_INVALID;
      } else {
        /* copy counter */
        memcpy(response.payload.aead_otp_decode.counter_timestamp, decoded + 8, THSM_PUBLIC_ID_SIZE);
      }

      /* clear temporary buffer */
      memset(decoded, 0, sizeof(decoded));
    } else {
      response.payload.aead_otp_decode.status = THSM_STATUS_AEAD_INVALID;
    }

    /* clear*/
    memset(recovered, 0, sizeof(recovered));
  }

  /* clear temporary buffer */
  memset(key, 0, sizeof(key));
}

static void cmd_db_otp_validate() {
  uint8_t  secret[THSM_KEY_SIZE + THSM_AEAD_NONCE_SIZE];

  uint8_t *src_pub  = request.payload.db_otp_validate.public_id;
  uint8_t *dst_pub  = response.payload.db_otp_validate.public_id;
  uint8_t *src_otp  = request.payload.db_otp_validate.otp;

  /* copy public id */
  memcpy(dst_pub, src_pub, THSM_PUBLIC_ID_SIZE);

  /* get key handle */
  uint8_t status = THSM_STATUS_OK;

  /* check parameters */
  if (request.bcnt > (sizeof(request.payload.db_otp_validate) + 1)) {
    response.payload.db_otp_validate.status = THSM_STATUS_INVALID_PARAMETER;
  } else if ((status = keystore_load_secret(secret, src_pub)) != THSM_STATUS_OK) {
    response.payload.db_otp_validate.status = status;
  } else {
    uint8_t decoded[THSM_BLOCK_SIZE]; // CRC16 || uid || counter || rand
    uint8_t *key = secret;
    uint8_t *uid_ref = secret  + THSM_KEY_SIZE;
    uint8_t *uid_act = decoded + 2;

    /* perform AES ECB decryption */
    aes_ecb_decrypt(decoded, src_otp, key, THSM_KEY_SIZE);

    /* compare CRC-16 */
    uint16_t crc = (decoded[0] << 8) | decoded[1];
    if (crc != CRC16.ccitt(decoded + 2, 14)) {
      response.payload.db_otp_validate.status = THSM_STATUS_OTP_INVALID;
    } else if (memcmp(uid_ref, uid_act, THSM_PUBLIC_ID_SIZE)) {
      response.payload.db_otp_validate.status = THSM_STATUS_OTP_INVALID;
    } else {
      /* copy counter */
      memcpy(response.payload.db_otp_validate.counter_timestamp, decoded + 8, THSM_PUBLIC_ID_SIZE);
    }

    /* clear temporary buffer */
    memset(decoded, 0, sizeof(decoded));
  }

  /* clear key buffer */
  memset(secret, 0, sizeof(secret));
}
