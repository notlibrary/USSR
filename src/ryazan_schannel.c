/*
 * ryazan_schannel.c - the Windows TLS backend for ryazan, on Schannel
 * (SSPI). TLS 1.2 and 1.3 as the system allows; certificates are checked
 * by CryptoAPI against the system roots, or against RYAZAN_CA_FILE.
 */

#include "ryazan_backend.h"

#ifdef RYAZAN_BACKEND_SCHANNEL

/* exclusive-root chain engines (Windows 8+) and SCH_CREDENTIALS (TLS 1.3) */
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#define WIN32_LEAN_AND_MEAN
#define SECURITY_WIN32
#define SCHANNEL_USE_BLACKLISTS

#include <windows.h>
#include <wincrypt.h>
#include <subauth.h>
#include <security.h>
#include <schannel.h>
#include <sspi.h>
#include <ncrypt.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _MSC_VER
#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "ncrypt.lib")
#pragma comment(lib, "advapi32.lib")
#endif

#ifndef SEC_E_INVALID_PARAMETER
#define SEC_E_INVALID_PARAMETER ((SECURITY_STATUS)0x8009035DL)
#endif
/* older mingw-w64 headers */
#ifndef SCH_USE_STRONG_CRYPTO
#define SCH_USE_STRONG_CRYPTO 0x00400000
#endif
#ifndef SP_PROT_TLS1_2_SERVER
#define SP_PROT_TLS1_2_SERVER 0x00000400
#define SP_PROT_TLS1_2_CLIENT 0x00000800
#endif
#ifndef SECBUFFER_ALERT
#define SECBUFFER_ALERT 17
#endif

typedef struct
{
    CtxtHandle context;
    int has_context;
    SecPkgContext_StreamSizes sizes;
    int verified;
    int renegotiating;  /* a post-handshake message is being processed */
    CredHandle *credential;
    WCHAR target[256];
} sc_conn_t;

/* one client credential; server credentials cached by file and secret */
static CredHandle client_credential;
static int has_client_credential;

typedef struct sc_server_cred
{
    char *credential;
    char *secret;
    CredHandle handle;
    HCERTSTORE store;
    PCCERT_CONTEXT certificate;
    struct sc_server_cred *next;
} sc_server_cred_t;

static sc_server_cred_t *server_credentials;

const char *rz_be_name(void) { return "schannel"; }

static void sc_fail(const char *what, SECURITY_STATUS status)
{
    char message[160] = "";
    DWORD length = FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
        (DWORD)status, 0, message, sizeof(message), NULL);

    while (length > 0 && (message[length - 1] == '\n' || message[length - 1] == '\r' ||
                          message[length - 1] == ' ' || message[length - 1] == '.'))
        message[--length] = '\0';
    rz_fail("%s: %s (0x%08lx)", what, length ? message : "Schannel error",
            (unsigned long)status);
}

static SECURITY_STATUS sc_acquire(DWORD direction, PCCERT_CONTEXT certificate,
                                  CredHandle *handle)
{
    SECURITY_STATUS status;
    TimeStamp expiry;
    DWORD flags = SCH_USE_STRONG_CRYPTO;

    if (direction == SECPKG_CRED_OUTBOUND)
        flags |= SCH_CRED_NO_DEFAULT_CREDS | SCH_CRED_MANUAL_CRED_VALIDATION;

#ifdef SCH_CREDENTIALS_VERSION
    {
        SCH_CREDENTIALS credentials;

        memset(&credentials, 0, sizeof(credentials));
        credentials.dwVersion = SCH_CREDENTIALS_VERSION;
        credentials.dwFlags = flags;
        if (certificate != NULL)
        {
            credentials.cCreds = 1;
            credentials.paCred = &certificate;
        }
        status = AcquireCredentialsHandleW(NULL, (LPWSTR)UNISP_NAME_W, direction,
                                           NULL, &credentials, NULL, NULL,
                                           handle, &expiry);
        if (status == SEC_E_OK)
            return status;
    }
#endif
    {
        /* older Windows: SCHANNEL_CRED, TLS 1.2 */
        SCHANNEL_CRED credentials;

        memset(&credentials, 0, sizeof(credentials));
        credentials.dwVersion = SCHANNEL_CRED_VERSION;
        credentials.dwFlags = flags;
        credentials.grbitEnabledProtocols =
            direction == SECPKG_CRED_OUTBOUND ? SP_PROT_TLS1_2_CLIENT
                                              : SP_PROT_TLS1_2_SERVER;
        if (certificate != NULL)
        {
            credentials.cCreds = 1;
            credentials.paCred = &certificate;
        }
        status = AcquireCredentialsHandleW(NULL, (LPWSTR)UNISP_NAME_W, direction,
                                           NULL, &credentials, NULL, NULL,
                                           handle, &expiry);
    }
    return status;
}

static sc_conn_t *sc_new(rz_conn_t *conn)
{
    sc_conn_t *sc = calloc(1, sizeof(*sc));

    if (sc == NULL)
        rz_fail("out of memory");
    conn->backend = sc;
    return sc;
}

int rz_be_client(rz_conn_t *conn)
{
    sc_conn_t *sc;

    if (!has_client_credential)
    {
        SECURITY_STATUS status = sc_acquire(SECPKG_CRED_OUTBOUND, NULL,
                                            &client_credential);
        if (status != SEC_E_OK)
        {
            sc_fail("cannot get TLS client credentials", status);
            return RZ_FAIL;
        }
        has_client_credential = 1;
    }
    sc = sc_new(conn);
    if (sc == NULL)
        return RZ_FAIL;
    sc->credential = &client_credential;
    if (MultiByteToWideChar(CP_UTF8, 0, conn->host, -1, sc->target, 256) == 0)
    {
        rz_fail("bad host name %.100s", conn->host);
        return RZ_FAIL;
    }
    return RZ_OK;
}

/* the certificate in store that has a private key */
static PCCERT_CONTEXT sc_find_keyed_certificate(HCERTSTORE store)
{
    PCCERT_CONTEXT certificate = NULL;

    while ((certificate = CertEnumCertificatesInStore(store, certificate)) != NULL)
    {
        DWORD size = 0;

        if (CertGetCertificateContextProperty(certificate, CERT_KEY_PROV_INFO_PROP_ID, NULL, &size) ||
            CertGetCertificateContextProperty(certificate, CERT_NCRYPT_KEY_HANDLE_PROP_ID, NULL, &size) ||
            CertGetCertificateContextProperty(certificate, CERT_KEY_CONTEXT_PROP_ID, NULL, &size))
            return certificate;     /* the caller frees it */
    }
    return NULL;
}

/* Removes the key PFXImportCertStore persisted for certificate. */
static void sc_delete_key(PCCERT_CONTEXT certificate)
{
    CRYPT_KEY_PROV_INFO *info;
    DWORD size = 0;

    if (!CertGetCertificateContextProperty(certificate, CERT_KEY_PROV_INFO_PROP_ID, NULL, &size) ||
        (info = malloc(size)) == NULL)
        return;
    if (CertGetCertificateContextProperty(certificate, CERT_KEY_PROV_INFO_PROP_ID, info, &size))
    {
        if (info->dwProvType == 0)
        {
            NCRYPT_PROV_HANDLE provider;
            NCRYPT_KEY_HANDLE key;

            if (NCryptOpenStorageProvider(&provider, info->pwszProvName, 0) == ERROR_SUCCESS)
            {
                if (NCryptOpenKey(provider, &key, info->pwszContainerName, 0,
                                  info->dwFlags & NCRYPT_MACHINE_KEY_FLAG) == ERROR_SUCCESS)
                    NCryptDeleteKey(key, 0);    /* also frees the handle */
                NCryptFreeObject(provider);
            }
        }
        else
        {
            HCRYPTPROV provider;

            CryptAcquireContextW(&provider, info->pwszContainerName, info->pwszProvName,
                                 info->dwProvType,
                                 CRYPT_DELETEKEYSET | (info->dwFlags & CRYPT_MACHINE_KEYSET));
        }
    }
    free(info);
}

static sc_server_cred_t *sc_server_credential(const char *credential, const char *secret)
{
    sc_server_cred_t *entry;
    rz_buf_t contents = { NULL, 0, 0 };
    CRYPT_DATA_BLOB blob;
    WCHAR password[256];
    SECURITY_STATUS status;
    size_t length = strlen(credential);

    for (entry = server_credentials; entry != NULL; entry = entry->next)
        if (strcmp(entry->credential, credential) == 0 && strcmp(entry->secret, secret) == 0)
            return entry;

    if (length < 4 || (_stricmp(credential + length - 4, ".p12") != 0 &&
                       _stricmp(credential + length - 4, ".pfx") != 0))
    {
        rz_fail("on Windows the server certificate must be a .p12 or .pfx file "
                "(PKCS#12 with its private key); convert PEM with: "
                "openssl pkcs12 -export -in cert.pem -inkey key.pem -out server.p12");
        return NULL;
    }
    if (MultiByteToWideChar(CP_UTF8, 0, secret, -1, password, 256) == 0)
    {
        rz_fail("the certificate password is too long");
        return NULL;
    }
    if (rz_read_file(credential, &contents) != 0)
        return NULL;
    entry = calloc(1, sizeof(*entry));
    if (entry == NULL)
    {
        free(contents.data);
        rz_fail("out of memory");
        return NULL;
    }
    blob.cbData = (DWORD)contents.length;
    blob.pbData = contents.data;
    /* Schannel serves only with a key in a key store, not an in-memory
     * one: the key goes to the user's CNG store (CNG for the RSA-PSS
     * signatures of TLS 1.3) and sc_delete_key removes it again */
    entry->store = PFXImportCertStore(&blob, password, CRYPT_USER_KEYSET | PKCS12_PREFER_CNG_KSP);
    SecureZeroMemory(password, sizeof(password));
    SecureZeroMemory(contents.data, contents.length);
    free(contents.data);
    if (entry->store == NULL)
    {
        sc_fail("cannot read the PKCS#12 file (wrong password?)", (SECURITY_STATUS)GetLastError());
        free(entry);
        return NULL;
    }
    entry->certificate = sc_find_keyed_certificate(entry->store);
    if (entry->certificate == NULL)
    {
        rz_fail("%.200s holds no certificate with a private key", credential);
        CertCloseStore(entry->store, 0);
        free(entry);
        return NULL;
    }
    status = sc_acquire(SECPKG_CRED_INBOUND, entry->certificate, &entry->handle);
    if (status != SEC_E_OK)
    {
        sc_fail("cannot use the server certificate", status);
        sc_delete_key(entry->certificate);
        CertFreeCertificateContext(entry->certificate);
        CertCloseStore(entry->store, 0);
        free(entry);
        return NULL;
    }
    entry->credential = _strdup(credential);
    entry->secret = _strdup(secret);
    entry->next = server_credentials;
    server_credentials = entry;
    return entry;
}

int rz_be_server(rz_conn_t *conn, const char *credential, const char *secret)
{
    sc_server_cred_t *entry = sc_server_credential(credential, secret);
    sc_conn_t *sc;

    if (entry == NULL)
        return RZ_FAIL;
    sc = sc_new(conn);
    if (sc == NULL)
        return RZ_FAIL;
    sc->credential = &entry->handle;
    return RZ_OK;
}

/* PEM certificates of path into a new memory store, or NULL. */
static HCERTSTORE sc_load_ca_file(const char *path)
{
    rz_buf_t contents = { NULL, 0, 0 };
    HCERTSTORE store;
    const char *cursor;
    int added = 0;

    if (rz_read_file(path, &contents) != 0)
        return NULL;
    store = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, NULL);
    cursor = (const char *)contents.data;
    while (store != NULL && (cursor = strstr(cursor, "-----BEGIN CERTIFICATE-----")) != NULL)
    {
        const char *end = strstr(cursor, "-----END CERTIFICATE-----");
        BYTE der[16384];
        DWORD der_length = sizeof(der);

        if (end == NULL)
            break;
        end += 25;
        if (CryptStringToBinaryA(cursor, (DWORD)(end - cursor), CRYPT_STRING_BASE64HEADER,
                                 der, &der_length, NULL, NULL) &&
            CertAddEncodedCertificateToStore(store, X509_ASN_ENCODING, der, der_length,
                                             CERT_STORE_ADD_ALWAYS, NULL))
            ++added;
        cursor = end;
    }
    free(contents.data);
    if (store != NULL && added == 0)
    {
        CertCloseStore(store, 0);
        store = NULL;
    }
    if (store == NULL)
        rz_fail("RYAZAN_CA_FILE %.200s holds no PEM certificate", path);
    return store;
}

static int sc_verify(rz_conn_t *conn, sc_conn_t *sc)
{
    PCCERT_CONTEXT certificate = NULL;
    PCCERT_CHAIN_CONTEXT chain = NULL;
    CERT_CHAIN_PARA chain_para;
    CERT_CHAIN_POLICY_PARA policy_para;
    CERT_CHAIN_POLICY_STATUS policy_status;
    HTTPSPolicyCallbackData https;
    HCERTCHAINENGINE engine = NULL;
    HCERTSTORE roots = NULL;
    LPSTR usages[] = { szOID_PKIX_KP_SERVER_AUTH, szOID_SERVER_GATED_CRYPTO, szOID_SGC_NETSCAPE };
    char *ca_file = rz_ca_file();
    SECURITY_STATUS status;
    int result = RZ_FAIL;

    status = QueryContextAttributesW(&sc->context, SECPKG_ATTR_REMOTE_CERT_CONTEXT, &certificate);
    if (status != SEC_E_OK || certificate == NULL)
    {
        sc_fail("the server sent no certificate", status);
        free(ca_file);
        return RZ_FAIL;
    }
    if (ca_file != NULL)
    {
        CERT_CHAIN_ENGINE_CONFIG config;

        roots = sc_load_ca_file(ca_file);
        if (roots == NULL)
            goto done;
        memset(&config, 0, sizeof(config));
        config.cbSize = sizeof(config);
        config.hExclusiveRoot = roots;
        config.dwExclusiveFlags = CERT_CHAIN_EXCLUSIVE_ENABLE_CA_FLAG;
        if (!CertCreateCertificateChainEngine(&config, &engine))
        {
            sc_fail("cannot use RYAZAN_CA_FILE", (SECURITY_STATUS)GetLastError());
            goto done;
        }
    }

    memset(&chain_para, 0, sizeof(chain_para));
    chain_para.cbSize = sizeof(chain_para);
    chain_para.RequestedUsage.dwType = USAGE_MATCH_TYPE_OR;
    chain_para.RequestedUsage.Usage.cUsageIdentifier = 3;
    chain_para.RequestedUsage.Usage.rgpszUsageIdentifier = usages;
    if (!CertGetCertificateChain(engine, certificate, NULL, certificate->hCertStore,
                                 &chain_para, 0, NULL, &chain))
    {
        sc_fail("cannot build the certificate chain", (SECURITY_STATUS)GetLastError());
        goto done;
    }

    memset(&https, 0, sizeof(https));
    https.cbStruct = sizeof(https);
    https.dwAuthType = AUTHTYPE_SERVER;
    https.pwszServerName = sc->target;
    memset(&policy_para, 0, sizeof(policy_para));
    policy_para.cbSize = sizeof(policy_para);
    policy_para.pvExtraPolicyPara = &https;
    memset(&policy_status, 0, sizeof(policy_status));
    policy_status.cbSize = sizeof(policy_status);
    if (!CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain,
                                          &policy_para, &policy_status))
    {
        sc_fail("cannot check the certificate", (SECURITY_STATUS)GetLastError());
        goto done;
    }
    if (policy_status.dwError != 0)
    {
        char what[320];

        snprintf(what, sizeof(what), "certificate of %s rejected", conn->host);
        sc_fail(what, (SECURITY_STATUS)policy_status.dwError);
        goto done;
    }
    sc->verified = 1;
    result = RZ_OK;

done:
    if (chain != NULL)
        CertFreeCertificateChain(chain);
    if (engine != NULL)
        CertFreeCertificateChainEngine(engine);
    if (roots != NULL)
        CertCloseStore(roots, 0);
    CertFreeCertificateContext(certificate);
    free(ca_file);
    return result;
}

/* One InitializeSecurityContext/AcceptSecurityContext round over the
 * bytes in conn->in (input may be NULL for the client's first call). */
static SECURITY_STATUS sc_round(rz_conn_t *conn, sc_conn_t *sc, int with_input,
                                size_t *extra)
{
    SecBuffer in_buffers[2];
    SecBuffer out_buffers[2];
    SecBufferDesc in_desc;
    SecBufferDesc out_desc;
    SECURITY_STATUS status;
    ULONG attributes = 0;
    CredHandle *credential = sc->credential;

    in_buffers[0].BufferType = SECBUFFER_TOKEN;
    in_buffers[0].pvBuffer = conn->in.data;
    in_buffers[0].cbBuffer = (ULONG)conn->in.length;
    in_buffers[1].BufferType = SECBUFFER_EMPTY;
    in_buffers[1].pvBuffer = NULL;
    in_buffers[1].cbBuffer = 0;
    in_desc.ulVersion = SECBUFFER_VERSION;
    in_desc.cBuffers = 2;
    in_desc.pBuffers = in_buffers;

    out_buffers[0].BufferType = SECBUFFER_TOKEN;
    out_buffers[0].pvBuffer = NULL;
    out_buffers[0].cbBuffer = 0;
    out_buffers[1].BufferType = SECBUFFER_ALERT;
    out_buffers[1].pvBuffer = NULL;
    out_buffers[1].cbBuffer = 0;
    out_desc.ulVersion = SECBUFFER_VERSION;
    out_desc.cBuffers = 2;
    out_desc.pBuffers = out_buffers;

    if (conn->server)
        status = AcceptSecurityContext(
            credential, sc->has_context ? &sc->context : NULL, &in_desc,
            ASC_REQ_SEQUENCE_DETECT | ASC_REQ_REPLAY_DETECT | ASC_REQ_CONFIDENTIALITY |
                ASC_REQ_EXTENDED_ERROR | ASC_REQ_ALLOCATE_MEMORY | ASC_REQ_STREAM,
            0, &sc->context, &out_desc, &attributes, NULL);
    else
        status = InitializeSecurityContextW(
            credential, sc->has_context ? &sc->context : NULL, sc->target,
            ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY |
                ISC_REQ_EXTENDED_ERROR | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM |
                ISC_REQ_USE_SUPPLIED_CREDS,
            0, 0, with_input ? &in_desc : NULL, 0,
            sc->has_context ? NULL : &sc->context, &out_desc, &attributes, NULL);

    if (status == SEC_E_OK || status == SEC_I_CONTINUE_NEEDED ||
        status == SEC_I_INCOMPLETE_CREDENTIALS)
        sc->has_context = 1;

    if (out_buffers[0].pvBuffer != NULL)
    {
        if (out_buffers[0].cbBuffer > 0 &&
            rz_queue(conn, out_buffers[0].pvBuffer, out_buffers[0].cbBuffer) != 0)
            status = SEC_E_INSUFFICIENT_MEMORY;
        FreeContextBuffer(out_buffers[0].pvBuffer);
    }
    if (out_buffers[1].pvBuffer != NULL)
        FreeContextBuffer(out_buffers[1].pvBuffer);

    *extra = in_buffers[1].BufferType == SECBUFFER_EXTRA ? in_buffers[1].cbBuffer : 0;
    return status;
}

static int sc_handshake(rz_conn_t *conn, sc_conn_t *sc)
{
    for (;;)
    {
        SECURITY_STATUS status;
        size_t extra = 0;

        if (!conn->server && !sc->has_context)
            status = sc_round(conn, sc, 0, &extra);
        else
        {
            if (conn->in.length == 0)
                return RZ_NEED_MORE;
            status = sc_round(conn, sc, 1, &extra);
            if (status == SEC_E_INCOMPLETE_MESSAGE)
                return RZ_NEED_MORE;
            /* keep only the bytes Schannel did not use */
            rz_consume(&conn->in, conn->in.length - extra);
        }

        if (status == SEC_E_OK)
        {
            status = QueryContextAttributesW(&sc->context, SECPKG_ATTR_STREAM_SIZES, &sc->sizes);
            if (status != SEC_E_OK)
            {
                sc_fail("TLS stream sizes", status);
                return RZ_FAIL;
            }
            if (!conn->server && !sc->verified && sc_verify(conn, sc) != RZ_OK)
                return RZ_FAIL;
            return RZ_OK;
        }
        if (status == SEC_I_CONTINUE_NEEDED || status == SEC_I_INCOMPLETE_CREDENTIALS)
            continue;   /* sends our reply, then waits for more input */
        sc_fail("TLS handshake failed", status);
        return RZ_FAIL;
    }
}

int rz_be_handshake(rz_conn_t *conn)
{
    return sc_handshake(conn, (sc_conn_t *)conn->backend);
}

int rz_be_decrypt(rz_conn_t *conn)
{
    sc_conn_t *sc = (sc_conn_t *)conn->backend;

    for (;;)
    {
        SecBuffer buffers[4];
        SecBufferDesc desc;
        SECURITY_STATUS status;
        size_t extra = 0;
        int i;

        buffers[0].BufferType = SECBUFFER_DATA;
        buffers[0].pvBuffer = conn->in.data;
        buffers[0].cbBuffer = (ULONG)conn->in.length;
        for (i = 1; i < 4; ++i)
        {
            buffers[i].BufferType = SECBUFFER_EMPTY;
            buffers[i].pvBuffer = NULL;
            buffers[i].cbBuffer = 0;
        }
        desc.ulVersion = SECBUFFER_VERSION;
        desc.cBuffers = 4;
        desc.pBuffers = buffers;

        if (sc->renegotiating)
        {
            /* TLS 1.3 post-handshake message (session ticket, key
             * update): Schannel takes it through the handshake call */
            int step = sc_handshake(conn, sc);

            if (step != RZ_OK)
                return step;
            sc->renegotiating = 0;
            continue;
        }
        if (conn->in.length == 0)
            return RZ_OK;
        buffers[0].pvBuffer = conn->in.data;
        buffers[0].cbBuffer = (ULONG)conn->in.length;
        status = DecryptMessage(&sc->context, &desc, 0, NULL);
        if (status == SEC_E_INCOMPLETE_MESSAGE)
            return RZ_NEED_MORE;
        if (status != SEC_E_OK && status != SEC_I_RENEGOTIATE &&
            status != SEC_I_CONTEXT_EXPIRED)
        {
            sc_fail("TLS decrypt failed", status);
            return RZ_FAIL;
        }
        for (i = 1; i < 4; ++i)
        {
            if (buffers[i].BufferType == SECBUFFER_DATA && buffers[i].cbBuffer > 0 &&
                rz_plain_add(conn, buffers[i].pvBuffer, buffers[i].cbBuffer) != 0)
                return RZ_FAIL;
            if (buffers[i].BufferType == SECBUFFER_EXTRA)
                extra = buffers[i].cbBuffer;
        }
        /* decryption was in place: what remains is the trailing extra */
        rz_consume(&conn->in, conn->in.length - extra);

        if (status == SEC_I_CONTEXT_EXPIRED)
            return RZ_CLOSED;
        if (status == SEC_I_RENEGOTIATE)
            sc->renegotiating = 1;
    }
}

int rz_be_encrypt(rz_conn_t *conn, const void *data, size_t length)
{
    sc_conn_t *sc = (sc_conn_t *)conn->backend;
    size_t offset = 0;
    size_t record_size = sc->sizes.cbHeader + sc->sizes.cbMaximumMessage + sc->sizes.cbTrailer;
    unsigned char *record = malloc(record_size);

    if (record == NULL)
    {
        rz_fail("out of memory");
        return RZ_FAIL;
    }
    while (offset < length)
    {
        size_t piece = length - offset;
        SecBuffer buffers[4];
        SecBufferDesc desc;
        SECURITY_STATUS status;

        if (piece > sc->sizes.cbMaximumMessage)
            piece = sc->sizes.cbMaximumMessage;
        memcpy(record + sc->sizes.cbHeader, (const unsigned char *)data + offset, piece);
        buffers[0].BufferType = SECBUFFER_STREAM_HEADER;
        buffers[0].pvBuffer = record;
        buffers[0].cbBuffer = sc->sizes.cbHeader;
        buffers[1].BufferType = SECBUFFER_DATA;
        buffers[1].pvBuffer = record + sc->sizes.cbHeader;
        buffers[1].cbBuffer = (ULONG)piece;
        buffers[2].BufferType = SECBUFFER_STREAM_TRAILER;
        buffers[2].pvBuffer = record + sc->sizes.cbHeader + piece;
        buffers[2].cbBuffer = sc->sizes.cbTrailer;
        buffers[3].BufferType = SECBUFFER_EMPTY;
        buffers[3].pvBuffer = NULL;
        buffers[3].cbBuffer = 0;
        desc.ulVersion = SECBUFFER_VERSION;
        desc.cBuffers = 4;
        desc.pBuffers = buffers;

        status = EncryptMessage(&sc->context, 0, &desc, 0);
        if (status != SEC_E_OK)
        {
            free(record);
            sc_fail("TLS encrypt failed", status);
            return RZ_FAIL;
        }
        if (rz_queue(conn, record,
                     buffers[0].cbBuffer + buffers[1].cbBuffer + buffers[2].cbBuffer) != 0)
        {
            free(record);
            return RZ_FAIL;
        }
        offset += piece;
    }
    free(record);
    return RZ_OK;
}

void rz_be_shutdown(rz_conn_t *conn)
{
    sc_conn_t *sc = (sc_conn_t *)conn->backend;
    DWORD type = SCHANNEL_SHUTDOWN;
    SecBuffer buffer;
    SecBufferDesc desc;
    size_t extra;

    if (sc == NULL || !sc->has_context)
        return;
    buffer.BufferType = SECBUFFER_TOKEN;
    buffer.pvBuffer = &type;
    buffer.cbBuffer = sizeof(type);
    desc.ulVersion = SECBUFFER_VERSION;
    desc.cBuffers = 1;
    desc.pBuffers = &buffer;
    if (ApplyControlToken(&sc->context, &desc) != SEC_E_OK)
        return;
    /* the next round produces the close_notify alert */
    conn->in.length = 0;
    (void)sc_round(conn, sc, conn->server, &extra);
}

#ifdef SECPKGCONTEXT_CIPHERINFO_V1
void rz_be_info(rz_conn_t *conn, char *text, size_t size)
{
    sc_conn_t *sc = (sc_conn_t *)conn->backend;
    SecPkgContext_CipherInfo info;
    const char *protocol = "TLS";
    char suite[64] = "";

    text[0] = '\0';
    memset(&info, 0, sizeof(info));
    info.dwVersion = SECPKGCONTEXT_CIPHERINFO_V1;
    if (sc == NULL || !sc->has_context ||
        QueryContextAttributesW(&sc->context, SECPKG_ATTR_CIPHER_INFO, &info) != SEC_E_OK)
        return;
    /* the wire version: 0x0304 is TLS 1.3 */
    if (info.dwProtocol == 0x0304)
        protocol = "TLS 1.3";
    else if (info.dwProtocol == 0x0303)
        protocol = "TLS 1.2";
    WideCharToMultiByte(CP_UTF8, 0, info.szCipherSuite, -1, suite, sizeof(suite), NULL, NULL);
    snprintf(text, size, "%s, %s", protocol, suite);
}
#else
/* headers without SecPkgContext_CipherInfo: the protocol alone */
void rz_be_info(rz_conn_t *conn, char *text, size_t size)
{
    sc_conn_t *sc = (sc_conn_t *)conn->backend;
    SecPkgContext_ConnectionInfo info;

    text[0] = '\0';
    if (sc == NULL || !sc->has_context ||
        QueryContextAttributesW(&sc->context, SECPKG_ATTR_CONNECTION_INFO, &info) != SEC_E_OK)
        return;
    snprintf(text, size, "%s", (info.dwProtocol & 0x3000) ? "TLS 1.3"
                               : (info.dwProtocol & 0x0C00) ? "TLS 1.2" : "TLS");
}
#endif

void rz_be_free(rz_conn_t *conn)
{
    sc_conn_t *sc = (sc_conn_t *)conn->backend;

    if (sc == NULL)
        return;
    if (sc->has_context)
        DeleteSecurityContext(&sc->context);
    free(sc);
    conn->backend = NULL;
}

void rz_be_cleanup(void)
{
    while (server_credentials != NULL)
    {
        sc_server_cred_t *next = server_credentials->next;

        FreeCredentialsHandle(&server_credentials->handle);
        sc_delete_key(server_credentials->certificate);
        CertFreeCertificateContext(server_credentials->certificate);
        CertCloseStore(server_credentials->store, 0);
        free(server_credentials->credential);
        if (server_credentials->secret != NULL)
            SecureZeroMemory(server_credentials->secret, strlen(server_credentials->secret));
        free(server_credentials->secret);
        free(server_credentials);
        server_credentials = next;
    }
    if (has_client_credential)
    {
        FreeCredentialsHandle(&client_credential);
        has_client_credential = 0;
    }
}

#else

/* ISO C forbids an empty translation unit */
typedef int ryazan_schannel_unused;

#endif /* RYAZAN_BACKEND_SCHANNEL */
