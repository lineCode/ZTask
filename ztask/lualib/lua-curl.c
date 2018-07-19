#define LUA_LIB

#include "ztask.h"
#include "ztask_curl.h"

#include <lua.h>
#include <lauxlib.h>
#include <stdlib.h>
#include <string.h>

#define LUACURL_EASYMETATABLE "CURL.easy"

typedef struct l_option_slist {
    CURLoption option;
    struct curl_slist *slist;
} l_option_slist;

//EASY上下文
typedef struct l_easy_private {
    CURL *curl;
    char error[CURL_ERROR_SIZE];

    /* slists, allocated by l_easy_setopt_strings */
    l_option_slist *option_slists;
} l_easy_private;

#if 1
static const struct {
    char *name;             /* human readable name: used as table index */
    unsigned long value;    /* CURLPROTO_* value */
} luacurl_protos[] = {
#ifdef CURLPROTO_HTTP
    { "HTTP", CURLPROTO_HTTP },
#endif
#ifdef CURLPROTO_HTTPS
{ "HTTPS", CURLPROTO_HTTPS },
#endif
#ifdef CURLPROTO_FTP
{ "FTP", CURLPROTO_FTP },
#endif
#ifdef CURLPROTO_FTPS
{ "FTPS", CURLPROTO_FTPS },
#endif
#ifdef CURLPROTO_SCP
{ "SCP", CURLPROTO_SCP },
#endif
#ifdef CURLPROTO_SFTP
{ "SFTP", CURLPROTO_SFTP },
#endif
#ifdef CURLPROTO_TELNET
{ "TELNET", CURLPROTO_TELNET },
#endif
#ifdef CURLPROTO_LDAP
{ "LDAP", CURLPROTO_LDAP },
#endif
#ifdef CURLPROTO_LDAPS
{ "LDAPS", CURLPROTO_LDAPS },
#endif
#ifdef CURLPROTO_DICT
{ "DICT", CURLPROTO_DICT },
#endif
#ifdef CURLPROTO_FILE
{ "FILE", CURLPROTO_FILE },
#endif
#ifdef CURLPROTO_TFTP
{ "TFTP", CURLPROTO_TFTP },
#endif
#ifdef CURLPROTO_IMAP
{ "IMAP", CURLPROTO_IMAP },
#endif
#ifdef CURLPROTO_IMAPS
{ "IMAPS", CURLPROTO_IMAPS },
#endif
#ifdef CURLPROTO_POP3
{ "POP3", CURLPROTO_POP3 },
#endif
#ifdef CURLPROTO_POP3S
{ "POP3S", CURLPROTO_POP3S },
#endif
#ifdef CURLPROTO_SMTP
{ "SMTP", CURLPROTO_SMTP },
#endif
#ifdef CURLPROTO_SMTPS
{ "SMTPS", CURLPROTO_SMTPS },
#endif
#ifdef CURLPROTO_RTSP
{ "RTSP", CURLPROTO_RTSP },
#endif
#ifdef CURLPROTO_RTMP
{ "RTMP", CURLPROTO_RTMP },
#endif
#ifdef CURLPROTO_RTMPT
{ "RTMPT", CURLPROTO_RTMPT },
#endif
#ifdef CURLPROTO_RTMPE
{ "RTMPE", CURLPROTO_RTMPE },
#endif
#ifdef CURLPROTO_RTMPTE
{ "RTMPTE", CURLPROTO_RTMPTE },
#endif
#ifdef CURLPROTO_RTMPS
{ "RTMPS", CURLPROTO_RTMPS },
#endif
#ifdef CURLPROTO_RTMPTS
{ "RTMPTS", CURLPROTO_RTMPTS },
#endif
#ifdef CURLPROTO_GOPHER
{ "GOPHER", CURLPROTO_GOPHER },
#endif
#ifdef CURLPROTO_ALL
{ "ALL", CURLPROTO_ALL },
#endif
{ NULL, 0 }
};

static void l_protocols_register(lua_State *L) {
    size_t i;

    lua_createtable(L, 0, sizeof luacurl_protos / sizeof luacurl_protos[0]);

    for (i = 0; luacurl_protos[i].name != NULL; i++) {
        lua_pushinteger(L, luacurl_protos[i].value);
        lua_setfield(L, -2, luacurl_protos[i].name);
    }

    lua_setfield(L, -2, "protocols");
}
#endif

#if 1 

#define P "setopt_"
#define LUACURL_OPTIONP_UPVALUE(L, INDEX) ((CURLoption *) lua_touserdata(L, lua_upvalueindex(INDEX)))

static int l_easy_setopt_long(lua_State *L) {
    l_easy_private *privatep = luaL_checkudata(L, 1, LUACURL_EASYMETATABLE);
    CURL *curl = privatep->curl;
    CURLoption *optionp = LUACURL_OPTIONP_UPVALUE(L, 1);
    long value = luaL_checkinteger(L, 2);

    if (curl_easy_setopt(curl, *optionp, value) != CURLE_OK)
        luaL_error(L, "%s", privatep->error);
    return 0;
}

static int l_easy_setopt_string(lua_State *L) {
    l_easy_private *privatep = luaL_checkudata(L, 1, LUACURL_EASYMETATABLE);
    CURL *curl = privatep->curl;
    CURLoption *optionp = LUACURL_OPTIONP_UPVALUE(L, 1);
    const char *value = luaL_checkstring(L, 2);

    if (curl_easy_setopt(curl, *optionp, value) != CURLE_OK)
        luaL_error(L, "%s", privatep->error);
    return 0;
}

void l_easy_setopt_free_slist(l_easy_private *privp, CURLoption option) {
    int i = 0;

    while (privp->option_slists[i].option != 0) {
        if (privp->option_slists[i].option == option) {
            /* free existing slist for this option */
            if (privp->option_slists[i].slist != NULL) {
                curl_slist_free_all(privp->option_slists[i].slist);
                privp->option_slists[i].slist = NULL;
            }
            break;
        }
        i++;
    }
}

struct curl_slist**  l_easy_setopt_get_slist(l_easy_private *privp, CURLoption option) {
    int i = 0;

    while (privp->option_slists[i].option != 0) {
        if (privp->option_slists[i].option == option)
            return &(privp->option_slists[i].slist);
        i++;
    }
    return NULL;
}

static int l_easy_setopt_strings(lua_State *L) {
    l_easy_private *privatep = luaL_checkudata(L, 1, LUACURL_EASYMETATABLE);
    CURL *curl = privatep->curl;
    CURLoption *optionp = LUACURL_OPTIONP_UPVALUE(L, 1);
    struct curl_slist *headerlist = NULL;
    int i = 1;

    /* free previous slist for this option */
    l_easy_setopt_free_slist(privatep, *optionp);

    if (lua_isstring(L, 2))
        *l_easy_setopt_get_slist(privatep, *optionp) = curl_slist_append(headerlist, lua_tostring(L, 2));
    else {
        if (lua_type(L, 2) != LUA_TTABLE)
            luaL_error(L, "wrong argument (%s): expected string or table", lua_typename(L, 2));

        lua_rawgeti(L, 2, i++);
        while (!lua_isnil(L, -1)) {
            struct curl_slist *current_slist = *l_easy_setopt_get_slist(privatep, *optionp);
            struct curl_slist *new_slist = curl_slist_append(current_slist, lua_tostring(L, -1));
            *l_easy_setopt_get_slist(privatep, *optionp) = new_slist;
            lua_pop(L, 1);
            lua_rawgeti(L, 2, i++);
        }
        lua_pop(L, 1);
    }

    if (curl_easy_setopt(curl, *optionp, *l_easy_setopt_get_slist(privatep, *optionp)) != CURLE_OK)
        luaL_error(L, "%s", privatep->error);
    /* memory leak: we need to free this in __gc */
    /*   curl_slist_free_all(headerlist);  */
    return 0;
}

static int l_easy_setopt_proxytype(lua_State *L) {
    l_easy_private *privatep = luaL_checkudata(L, 1, LUACURL_EASYMETATABLE);
    CURL *curl = privatep->curl;
    CURLoption *optionp = LUACURL_OPTIONP_UPVALUE(L, 1);
    const char *value = luaL_checkstring(L, 2);

    /* check for valid OPTION: */
    curl_proxytype type;

    if (!strcmp("HTTP", value))
        type = CURLPROXY_HTTP;
    else if (!strcmp("SOCKS4", value))
        type = CURLPROXY_SOCKS4;
    else if (!strcmp("SOCKS5", value))
        type = CURLPROXY_SOCKS5;
    else
        luaL_error(L, "Invalid proxytype: %s", value);

    if (curl_easy_setopt(curl, *optionp, type) != CURLE_OK)
        luaL_error(L, "%s", privatep->error);
    return 0;
}

static int l_easy_setopt_httpauth(lua_State *L) {
    l_easy_private *privatep = luaL_checkudata(L, 1, LUACURL_EASYMETATABLE);
    CURL *curl = privatep->curl;
    CURLoption *optionp = LUACURL_OPTIONP_UPVALUE(L, 1);
    const char *value = luaL_checkstring(L, 2);

    long type;

    if (!strcmp("NONE", value))
        type = CURLAUTH_NONE;
    else if (!strcmp("BASIC", value))
        type = CURLAUTH_BASIC;
    else if (!strcmp("DIGEST", value))
        type = CURLAUTH_DIGEST;
    else if (!strcmp("GSSNEGOTIATE", value))
        type = CURLAUTH_GSSNEGOTIATE;
    else if (!strcmp("NTLM", value))
        type = CURLAUTH_NTLM;
    else if (!strcmp("CURLAUTH_ANY", value))
        type = CURLAUTH_ANY;
    else if (!strcmp("ANYSAFE", value))
        type = CURLAUTH_ANYSAFE;
    else
        luaL_error(L, "Invalid httpauth: %s", value);

    if (curl_easy_setopt(curl, *optionp, type) != CURLE_OK)
        luaL_error(L, "%s", privatep->error);
    return 0;
}



/* closures assigned to setopt in setopt table */
static struct {
    const char *name;
    CURLoption option;
    lua_CFunction func;
} luacurl_setopt_c[] = {
    /* behavior options */
    { P"verbose", CURLOPT_VERBOSE, l_easy_setopt_long },
{ P"header", CURLOPT_HEADER, l_easy_setopt_long },
{ P"noprogress", CURLOPT_NOPROGRESS, l_easy_setopt_long },
{ P"nosignal", CURLOPT_NOSIGNAL, l_easy_setopt_long },
/* callback options */
/* network options */
/* names and passwords options  */
/* http options */

{ P"autoreferer", CURLOPT_AUTOREFERER, l_easy_setopt_long },
{ P"encoding", CURLOPT_ENCODING, l_easy_setopt_string },
{ P"followlocation", CURLOPT_FOLLOWLOCATION, l_easy_setopt_long },
{ P"unrestricted_AUTH", CURLOPT_UNRESTRICTED_AUTH, l_easy_setopt_long },
{ P"maxredirs", CURLOPT_MAXREDIRS, l_easy_setopt_long },
/* not implemented */
/*   {P"put", CURLOPT_PUT, l_easy_setopt_long}, */
{ P"post", CURLOPT_POST, l_easy_setopt_long },
{ P"postfields", CURLOPT_POSTFIELDS, l_easy_setopt_string },
{ P"postfieldsize", CURLOPT_POSTFIELDSIZE, l_easy_setopt_long },
{ P"postfieldsize_LARGE", CURLOPT_POSTFIELDSIZE_LARGE, l_easy_setopt_long },
{ P"httppost", CURLOPT_HTTPPOST, l_easy_setopt_long },
{ P"referer", CURLOPT_REFERER, l_easy_setopt_string },
{ P"useragent", CURLOPT_USERAGENT, l_easy_setopt_string },
{ P"httpheader", CURLOPT_HTTPHEADER, l_easy_setopt_strings },
{ P"httpauth", CURLOPT_HTTPAUTH, l_easy_setopt_httpauth },
{ P"timeout", CURLOPT_TIMEOUT, l_easy_setopt_long },
/*  Not implemented:  {P"http200aliases", CURLOPT_HTTP200ALIASES, l_easy_setopt_long}, */
{ P"cookie", CURLOPT_COOKIE, l_easy_setopt_string },
{ P"cookiefile", CURLOPT_COOKIEFILE, l_easy_setopt_string },
{ P"cookiejar", CURLOPT_COOKIEJAR, l_easy_setopt_string },
{ P"cookiesession", CURLOPT_COOKIESESSION, l_easy_setopt_long },
#ifdef CURLOPT_COOKIELIST
{ P"cookielist", CURLOPT_COOKIELIST, l_easy_setopt_string },
#endif
{ P"httpget", CURLOPT_HTTPGET, l_easy_setopt_long },
/*  Not implemented: {P"http_version", CURLOPT_HTTP_VERSION, l_easy_setopt_long}, */
{ P"ignore_content_length", CURLOPT_IGNORE_CONTENT_LENGTH, l_easy_setopt_long },
#ifdef CURLOPT_HTTP_CONTENT_DECODING
{ P"http_content_decoding", CURLOPT_HTTP_CONTENT_DECODING, l_easy_setopt_long },
#endif
#ifdef CURLOPT_HTTP_TRANSFER_DECODING
{ P"http_transfer_decoding ", CURLOPT_HTTP_TRANSFER_DECODING , l_easy_setopt_long },
#endif
/* ftp options */
/* protocol options */
{ P"transfertext", CURLOPT_TRANSFERTEXT, l_easy_setopt_long },
{ P"crlf", CURLOPT_CRLF, l_easy_setopt_long },
{ P"range", CURLOPT_RANGE, l_easy_setopt_string },
{ P"resume_from", CURLOPT_RESUME_FROM, l_easy_setopt_long },
{ P"resume_from_large", CURLOPT_RESUME_FROM_LARGE, l_easy_setopt_long },
{ P"customrequest", CURLOPT_CUSTOMREQUEST, l_easy_setopt_string },
{ P"filetime", CURLOPT_FILETIME, l_easy_setopt_long },
{ P"nobody", CURLOPT_NOBODY, l_easy_setopt_long },
{ P"infilesize", CURLOPT_INFILESIZE, l_easy_setopt_long },
{ P"infilesize_large", CURLOPT_INFILESIZE_LARGE, l_easy_setopt_long },
{ P"upload", CURLOPT_UPLOAD, l_easy_setopt_long },
{ P"maxfilesize", CURLOPT_MAXFILESIZE, l_easy_setopt_long },
{ P"maxfilesize_large", CURLOPT_MAXFILESIZE_LARGE, l_easy_setopt_long },
{ P"timecondition", CURLOPT_TIMECONDITION, l_easy_setopt_long },
{ P"timevalue ", CURLOPT_TIMEVALUE , l_easy_setopt_long },
/* network options */
{ P"url", CURLOPT_URL, l_easy_setopt_string },
{ P"protocols", CURLOPT_PROTOCOLS, l_easy_setopt_long },
{ P"redir_protocols", CURLOPT_REDIR_PROTOCOLS, l_easy_setopt_long },
{ P"proxy", CURLOPT_PROXY, l_easy_setopt_string },
{ P"username", CURLOPT_USERNAME, l_easy_setopt_string },
{ P"password", CURLOPT_PASSWORD, l_easy_setopt_string },
{ P"userpwd", CURLOPT_USERPWD, l_easy_setopt_string },
{ P"proxyuserpwd", CURLOPT_PROXYUSERPWD, l_easy_setopt_string },
{ P"proxyport", CURLOPT_PROXYPORT, l_easy_setopt_long },
{ P"proxytype", CURLOPT_PROXYTYPE, l_easy_setopt_proxytype },
{ P"httpproxytunnel", CURLOPT_HTTPPROXYTUNNEL, l_easy_setopt_long },
{ P"interface", CURLOPT_INTERFACE, l_easy_setopt_string },
{ P"localport", CURLOPT_LOCALPORT, l_easy_setopt_long },
{ P"localportrange", CURLOPT_LOCALPORTRANGE, l_easy_setopt_long },
{ P"dns_cache_timeout", CURLOPT_DNS_CACHE_TIMEOUT, l_easy_setopt_long },
{ P"dns_use_global_cache", CURLOPT_DNS_USE_GLOBAL_CACHE, l_easy_setopt_long },
{ P"buffersize", CURLOPT_BUFFERSIZE, l_easy_setopt_long },
{ P"port", CURLOPT_PORT, l_easy_setopt_long },
{ P"TCP_nodelay", CURLOPT_TCP_NODELAY, l_easy_setopt_long },
{ P"ssl_verifypeer", CURLOPT_SSL_VERIFYPEER, l_easy_setopt_long },
/* ssl options */
{ P"sslcert", CURLOPT_SSLCERT, l_easy_setopt_string },
{ P"sslcerttype", CURLOPT_SSLCERTTYPE, l_easy_setopt_string },
{ P"sslcertpasswd", CURLOPT_SSLCERTPASSWD, l_easy_setopt_string },
{ P"sslkey", CURLOPT_SSLKEY, l_easy_setopt_string },
{ P"sslkeytype", CURLOPT_SSLKEYTYPE, l_easy_setopt_string },
{ P"sslkeypasswd", CURLOPT_SSLKEYPASSWD, l_easy_setopt_string },
{ P"sslengine", CURLOPT_SSLENGINE, l_easy_setopt_string },
{ P"sslengine_default", CURLOPT_SSLENGINE_DEFAULT, l_easy_setopt_long },
/* not implemented  {P"sslversion", CURLOPT_SSLVERSION, l_easy_setopt_string}, */
{ P"ssl_verifypeer", CURLOPT_SSL_VERIFYPEER, l_easy_setopt_long },
{ P"cainfo", CURLOPT_CAINFO, l_easy_setopt_string },
{ P"capath", CURLOPT_CAPATH, l_easy_setopt_string },
{ P"random_file", CURLOPT_RANDOM_FILE, l_easy_setopt_string },
{ P"egdsocket", CURLOPT_EGDSOCKET, l_easy_setopt_string },
{ P"ssl_verifyhost", CURLOPT_SSL_VERIFYHOST, l_easy_setopt_long },
{ P"ssl_cipher_list", CURLOPT_SSL_CIPHER_LIST, l_easy_setopt_string },
#ifdef CURLOPT_SSL_SESSIONID_CACHE
{ P"ssl_sessionid_cache", CURLOPT_SSL_SESSIONID_CACHE, l_easy_setopt_long },
#endif
/* not implemented:   {P"krblevel", CURLOPT_KRBLEVEL, l_easy_setopt_string}, */
/* dummy opt value */
{ NULL, CURLOPT_VERBOSE, NULL } };

int l_easy_setopt_register(lua_State *L) {
    int i;

    /* register setopt closures */
    for (i = 0; luacurl_setopt_c[i].name != NULL; i++) {
        CURLoption *optionp = &(luacurl_setopt_c[i].option);
        lua_pushlightuserdata(L, optionp);
        lua_pushcclosure(L, luacurl_setopt_c[i].func, 1);
        lua_setfield(L, -2, luacurl_setopt_c[i].name);
    }

    return 0;
}

void  l_easy_setopt_init_slists(lua_State *L, l_easy_private *privp) {
    int i, n;

    /* count required slists */
    for (i = 0, n = 0; luacurl_setopt_c[i].name != NULL; i++)
        if (luacurl_setopt_c[i].func == l_easy_setopt_strings) n++;

    privp->option_slists = (l_option_slist*)ztask_malloc(sizeof(l_option_slist) * ++n);
    if (privp->option_slists == NULL)
        luaL_error(L, "can't malloc option slists");

    /* Init slists */
    for (i = 0, n = 0; luacurl_setopt_c[i].name != NULL; i++) {
        CURLoption option = luacurl_setopt_c[i].option;
        if (luacurl_setopt_c[i].func == l_easy_setopt_strings) {
            privp->option_slists[n].option = option;
            privp->option_slists[n].slist = NULL;
            n++;
        }
    }
    /* term list */
    privp->option_slists[n].option = 0;
    privp->option_slists[n].slist = NULL;
}

void  l_easy_setopt_free_slists(l_easy_private *privp) {
    int i = 0;

    while (privp->option_slists[i].option != 0) {
        if (privp->option_slists[i].slist != NULL)
            curl_slist_free_all(privp->option_slists[i].slist);
        i++;
    }

    ztask_free(privp->option_slists);
}

#endif

//对象GC
int l_easy_gc(lua_State *L) {
    /* gc resources optained by cURL userdata */
    l_easy_private *privp = lua_touserdata(L, 1);
    curl_easy_cleanup(privp->curl);
    l_easy_setopt_free_slists(privp);
    return 0;
}

int l_easy_init(lua_State *L) {
    l_easy_private *privp;

    /* create userdata and assign metatable */
    privp = (l_easy_private *)lua_newuserdata(L, sizeof(l_easy_private));

    /* allocate list of curl_slist for setopt handling */
    l_easy_setopt_init_slists(L, privp);

    luaL_getmetatable(L, LUACURL_EASYMETATABLE);
    lua_setmetatable(L, -2);

    if ((privp->curl = curl_easy_init()) == NULL)
        return luaL_error(L, "something went wrong and you cannot use the other curl functions");

    /* set error buffer */
    if (curl_easy_setopt(privp->curl, CURLOPT_ERRORBUFFER, privp->error) != CURLE_OK)
        return luaL_error(L, "cannot set error buffer");

    /* return userdata; */
    return 1;
}

int l_easy_perform(lua_State *L) {
    l_easy_private *privatep = luaL_checkudata(L, 1, LUACURL_EASYMETATABLE);
    CURL *curl = privatep->curl;
    uint32_t self = luaL_checkinteger(L, 2);
    int session = luaL_checkinteger(L, 3);
    
    ztask_curl(self, session, curl);

    return 0;
}
int l_easy_data(lua_State *L) {
    l_easy_private *privatep = luaL_checkudata(L, 1, LUACURL_EASYMETATABLE);
    CURL *curl = privatep->curl;
    struct ztask_curl_message *msg = lua_touserdata(L, 2);
    int sz = luaL_checkinteger(L, 3);
    if (msg) {
        lua_pushlightuserdata(L, (char *)msg + sizeof(struct ztask_curl_message) + msg->cookies_len + 1);
        lua_pushinteger(L, msg->data_len);
        if (msg->cookies_len) {
            lua_pushlstring(L, (char *)msg + sizeof(struct ztask_curl_message), msg->cookies_len);
            return 3;
        }
        return 2;
    }
    return 0;
}
/* methods assigned to easy table */
static const struct luaL_Reg luacurl_easy_m[] =
{ 
    { "data", l_easy_data },
    { "perform", l_easy_perform },
    { "__gc", l_easy_gc },
{ NULL, NULL } };

/* global functions in module namespace*/
static const struct luaL_Reg luacurl_f[] = {
    { "easy_init", l_easy_init },
{ NULL, NULL } };

LUAMOD_API int luaopen_curl_core(lua_State *L) {
    luaL_checkversion(L);

    /* EASY START */
    luaL_newmetatable(L, LUACURL_EASYMETATABLE);

    /* register in easymetatable */
    luaL_setfuncs(L, luacurl_easy_m, 0);


    /* easymetatable.__index = easymetatable */
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    /* register getinfo closures  */
    //l_easy_getinfo_register(L);
    /* register setopt closures  */
    l_easy_setopt_register(L);



    luaL_newlib(L, luacurl_f);
    lua_pushvalue(L, -1);
    lua_setglobal(L, "cURL");

    //注册常量表
    l_protocols_register(L);

    return 1;
}
