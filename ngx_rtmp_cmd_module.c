
/*
 * Original work Copyright (C) Roman Arutyunyan
 * Modified work Copyright (C) 2017 Gnolizuh
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include "ngx_rtmp_cmd_module.h"
#include "ngx_rtmp_streams.h"


#define NGX_RTMP_FMS_VERSION        "FMS/3,0,1,123"
#define NGX_RTMP_CAPABILITIES       31


static ngx_int_t ngx_rtmp_cmd_connect(ngx_rtmp_session_t *s,
       ngx_rtmp_connect_t *v);
static ngx_int_t ngx_rtmp_cmd_disconnect(ngx_rtmp_session_t *s);
static ngx_int_t ngx_rtmp_cmd_release_stream(ngx_rtmp_session_t *s,
       ngx_rtmp_release_stream_t *v);
static ngx_int_t ngx_rtmp_cmd_create_stream(ngx_rtmp_session_t *s,
       ngx_rtmp_create_stream_t *v);
static ngx_int_t ngx_rtmp_cmd_close_stream(ngx_rtmp_session_t *s,
       ngx_rtmp_close_stream_t *v);
static ngx_int_t ngx_rtmp_cmd_delete_stream(ngx_rtmp_session_t *s,
       ngx_rtmp_delete_stream_t *v);
static ngx_int_t ngx_rtmp_cmd_fcpublish(ngx_rtmp_session_t *s,
       ngx_rtmp_fcpublish_t *v);
static ngx_int_t ngx_rtmp_cmd_publish(ngx_rtmp_session_t *s,
       ngx_rtmp_publish_t *v);
static ngx_int_t ngx_rtmp_cmd_play(ngx_rtmp_session_t *s,
       ngx_rtmp_play_t *v);
static ngx_int_t ngx_rtmp_cmd_seek(ngx_rtmp_session_t *s,
       ngx_rtmp_seek_t *v);
static ngx_int_t ngx_rtmp_cmd_pause(ngx_rtmp_session_t *s,
       ngx_rtmp_pause_t *v);


static ngx_int_t ngx_rtmp_cmd_stream_begin(ngx_rtmp_session_t *s,
       ngx_rtmp_stream_begin_t *v);
static ngx_int_t ngx_rtmp_cmd_stream_eof(ngx_rtmp_session_t *s,
       ngx_rtmp_stream_eof_t *v);
static ngx_int_t ngx_rtmp_cmd_stream_dry(ngx_rtmp_session_t *s,
       ngx_rtmp_stream_dry_t *v);
static ngx_int_t ngx_rtmp_cmd_recorded(ngx_rtmp_session_t *s,
       ngx_rtmp_recorded_t *v);
static ngx_int_t ngx_rtmp_cmd_set_buflen(ngx_rtmp_session_t *s,
       ngx_rtmp_set_buflen_t *v);


ngx_rtmp_connect_pt         ngx_rtmp_connect;
ngx_rtmp_disconnect_pt      ngx_rtmp_disconnect;
ngx_rtmp_release_stream_pt  ngx_rtmp_release_stream;
ngx_rtmp_create_stream_pt   ngx_rtmp_create_stream;
ngx_rtmp_close_stream_pt    ngx_rtmp_close_stream;
ngx_rtmp_delete_stream_pt   ngx_rtmp_delete_stream;
ngx_rtmp_fcpublish_pt       ngx_rtmp_fcpublish;
ngx_rtmp_publish_pt         ngx_rtmp_publish;
ngx_rtmp_play_pt            ngx_rtmp_play;
ngx_rtmp_seek_pt            ngx_rtmp_seek;
ngx_rtmp_pause_pt           ngx_rtmp_pause;


ngx_rtmp_stream_begin_pt    ngx_rtmp_stream_begin;
ngx_rtmp_stream_eof_pt      ngx_rtmp_stream_eof;
ngx_rtmp_stream_dry_pt      ngx_rtmp_stream_dry;
ngx_rtmp_recorded_pt        ngx_rtmp_recorded;
ngx_rtmp_set_buflen_pt      ngx_rtmp_set_buflen;


static ngx_int_t ngx_rtmp_cmd_postconfiguration(ngx_conf_t *cf);


static ngx_rtmp_module_t  ngx_rtmp_cmd_module_ctx = {
    NULL,                                   /* preconfiguration */
    ngx_rtmp_cmd_postconfiguration,         /* postconfiguration */
    NULL,                                   /* create main configuration */
    NULL,                                   /* init main configuration */
    NULL,                                   /* create server configuration */
    NULL,                                   /* merge server configuration */
    NULL,                                   /* create service configuration */
    NULL,                                   /* merge service configuration */
    NULL,                                   /* create app configuration */
    NULL                                    /* merge app configuration */
};


ngx_module_t  ngx_rtmp_cmd_module = {
    NGX_MODULE_V1,
    &ngx_rtmp_cmd_module_ctx,               /* module context */
    NULL,                                   /* module directives */
    NGX_RTMP_MODULE,                        /* module type */
    NULL,                                   /* init master */
    NULL,                                   /* init module */
    NULL,                                   /* init process */
    NULL,                                   /* init thread */
    NULL,                                   /* exit thread */
    NULL,                                   /* exit process */
    NULL,                                   /* exit master */
    NGX_MODULE_V1_PADDING
};


void
ngx_rtmp_cmd_fill_args(u_char name[NGX_RTMP_MAX_NAME],
        u_char args[NGX_RTMP_MAX_ARGS])
{
    u_char      *p;

    p = (u_char *)ngx_strchr(name, '?');
    if (p == NULL) {
        return;
    }

    *p++ = 0;
    ngx_cpystrn(args, p, NGX_RTMP_MAX_ARGS);
}


static ngx_int_t
ngx_rtmp_cmd_get_conf(ngx_rtmp_session_t *s, const char *func)
{
    ngx_rtmp_core_app_conf_t  **cacfp;
    ngx_rtmp_host_name_t       *hn;
    ngx_hash_combined_t        *hash;
    ngx_uint_t                  n;

    hash = &s->addr_conf->virtual_hosts->names;
    if (hash == NULL) {
        ngx_log_error(NGX_LOG_EMERG, s->connection->log, 0,
                      "fetch virtual host failed, pls check your config file.");
        return NGX_ERROR;
    }

    /* match host to find out service conf */
    hn = ngx_hash_find_combined(hash, ngx_hash_key(s->host.data, s->host.len),
             s->host.data, s->host.len);
    if (hn == NULL || (hn->mask & s->host_mask) != s->host_mask) {
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "%s: service '%V' not found, use the default service.", func, &s->host);
        //added default service by Edward.Wu, to push and play stream with ip address or unknown hostname
        //in conf file: hostname direct-bi proto-all "";
		ngx_str_set(&s->host, "");
        hn = ngx_hash_find_combined(hash, ngx_hash_key(s->host.data, s->host.len),
                 s->host.data, s->host.len);
        if (hn == NULL || (hn->mask & s->host_mask) != s->host_mask) {
            ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                          "%s: service '%V' not match in the default service.", func, &s->host);
            return NGX_ERROR;
        }
    }

    /* found service */
    s->svi_conf = hn->service->svi_conf;
    s->service = hn->service->name;

    /* match application to find out app conf */
    cacfp = hn->service->applications.elts;
    for(n = 0; n < hn->service->applications.nelts; ++n, ++cacfp) {
        if ((*cacfp)->name.len == s->app.len &&
            ngx_strncmp((*cacfp)->name.data, s->app.data, s->app.len) == 0)
        {
            /* found app! */
            s->app_conf = (*cacfp)->app_conf;
            break;
        }
    }

    if (s->app_conf == NULL) {
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                      "%s: application not found: '%V'", func, &s->app);
        return NGX_ERROR;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_cmd_connect_init(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
        ngx_chain_t *in)
{
    ngx_str_t                   str;
    u_char                     *p;

    static ngx_rtmp_connect_t   v;

    static ngx_rtmp_amf_elt_t  in_cmd[] = {

        { NGX_RTMP_AMF_NUMBER,
          ngx_string("hostMask"),
          &v.host_mask, sizeof(v.host_mask) },

        { NGX_RTMP_AMF_STRING,
          ngx_string("host"),
          v.host, sizeof(v.host) },

        { NGX_RTMP_AMF_STRING,
          ngx_string("app"),
          v.app, sizeof(v.app) },

        { NGX_RTMP_AMF_STRING,
          ngx_string("flashVer"),
          v.flashver, sizeof(v.flashver) },

        { NGX_RTMP_AMF_STRING,
          ngx_string("swfUrl"),
          v.swf_url, sizeof(v.swf_url) },

        { NGX_RTMP_AMF_STRING,
          ngx_string("tcUrl"),
          v.tc_url, sizeof(v.tc_url) },

        { NGX_RTMP_AMF_NUMBER,
          ngx_string("audioCodecs"),
          &v.acodecs, sizeof(v.acodecs) },

        { NGX_RTMP_AMF_NUMBER,
          ngx_string("videoCodecs"),
          &v.vcodecs, sizeof(v.vcodecs) },

        { NGX_RTMP_AMF_STRING,
          ngx_string("pageUrl"),
          v.page_url, sizeof(v.page_url) },

        { NGX_RTMP_AMF_NUMBER,
          ngx_string("objectEncoding"),
          &v.object_encoding, 0},
    };

    static ngx_rtmp_amf_elt_t  in_elts[] = {

        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          &v.trans, 0 },

        { NGX_RTMP_AMF_OBJECT,
          ngx_null_string,
          in_cmd, sizeof(in_cmd) },
    };

    ngx_memzero(&v, sizeof(v));
    if (ngx_rtmp_receive_amf(s, in, in_elts,
                sizeof(in_elts) / sizeof(in_elts[0])))
    {
        return NGX_ERROR;
    }

    // get host
    if (ngx_strlen(v.host) == 0) {
        str.data = v.tc_url;
        str.len = ngx_strlen(v.tc_url);
        if (str.len > 7 && !ngx_memcmp(str.data, "rtmp://", 7)) {
            str.data += 7;
            str.len -= 7;
        }

        p = ngx_strlchr(str.data, str.data + str.len, ':');
        if (!p) {
            p = ngx_strlchr(str.data, str.data + str.len, '/');
            if (!p) {
                p = str.data + str.len;
            }
        }

        ngx_memcpy(v.host, str.data, p - str.data);
    }

    // get app
    str.data = v.app;
    str.len = ngx_strlen(v.app);
    if (str.len > 10 && !ngx_memcmp(str.data + str.len - 10, "/_definst_", 10)) {
        str.data[str.len - 10] = 0;
        str.len -= 10;
    } else if (str.len && v.app[str.len - 1] == '/') {
        str.data[str.len - 1] = 0;
        str.len -= 1;
    }

    ngx_rtmp_cmd_fill_args(v.app, v.args);

    // restructure app & host
    p = ngx_strlchr(str.data, str.data + str.len, '/');
    if (p) {
        *ngx_cpymem(v.host, str.data, p - str.data) = 0;
        *ngx_cpymem(v.app, p + 1, str.data + str.len - p - 1) = 0;
    }

    /* set host mask */
    s->host_mask |= v.host_mask > 0 ? (int) v.host_mask : NGX_RTMP_HOSTNAME_RTMP;

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
            "connect: host='%s' app='%s' args='%s' flashver='%s' swf_url='%s' "
            "tc_url='%s' page_url='%s' acodecs=%uD vcodecs=%uD "
            "object_encoding=%ui",
            v.host, v.app, v.args, v.flashver, v.swf_url, v.tc_url, v.page_url,
            (uint32_t)v.acodecs, (uint32_t)v.vcodecs,
            (ngx_int_t)v.object_encoding);

    return ngx_rtmp_connect(s, &v);
}


static ngx_int_t
ngx_rtmp_cmd_connect(ngx_rtmp_session_t *s, ngx_rtmp_connect_t *v)
{
    ngx_rtmp_core_srv_conf_t   *cscf;
    ngx_rtmp_header_t           h;

    static double               trans;
    static double               capabilities = NGX_RTMP_CAPABILITIES;
    static double               object_encoding = 0;

    static ngx_rtmp_amf_elt_t  out_obj[] = {

        { NGX_RTMP_AMF_STRING,
          ngx_string("fmsVer"),
          NGX_RTMP_FMS_VERSION, 0 },

        { NGX_RTMP_AMF_NUMBER,
          ngx_string("capabilities"),
          &capabilities, 0 },
    };

    static ngx_rtmp_amf_elt_t  out_inf[] = {

        { NGX_RTMP_AMF_STRING,
          ngx_string("level"),
          "status", 0 },

        { NGX_RTMP_AMF_STRING,
          ngx_string("code"),
          "NetConnection.Connect.Success", 0 },

        { NGX_RTMP_AMF_STRING,
          ngx_string("description"),
          "Connection succeeded.", 0 },

        { NGX_RTMP_AMF_NUMBER,
          ngx_string("objectEncoding"),
          &object_encoding, 0 }
    };

    static ngx_rtmp_amf_elt_t  out_elts[] = {

        { NGX_RTMP_AMF_STRING,
          ngx_null_string,
          "_result", 0 },

        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          &trans, 0 },

        { NGX_RTMP_AMF_OBJECT,
          ngx_null_string,
          out_obj, sizeof(out_obj) },

        { NGX_RTMP_AMF_OBJECT,
          ngx_null_string,
          out_inf, sizeof(out_inf) },
    };

    if (s->connected) {
        ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                "connect: duplicate connection");
        return NGX_ERROR;
    }

    cscf = ngx_rtmp_get_module_srv_conf(s, ngx_rtmp_core_module);

    trans = v->trans;

    /* fill session parameters */
    s->connected = 1;

    ngx_memzero(&h, sizeof(h));
    h.csid = NGX_RTMP_CSID_AMF_INI;
    h.type = NGX_RTMP_MSG_AMF_CMD;


#define NGX_RTMP_SET_STRPAR(name)                                             \
    s->name.len = ngx_strlen(v->name);                                        \
    s->name.data = ngx_palloc(s->connection->pool, s->name.len);              \
    ngx_memcpy(s->name.data, v->name, s->name.len)

    NGX_RTMP_SET_STRPAR(host);
    NGX_RTMP_SET_STRPAR(app);
    NGX_RTMP_SET_STRPAR(args);
    NGX_RTMP_SET_STRPAR(flashver);
    NGX_RTMP_SET_STRPAR(swf_url);
    NGX_RTMP_SET_STRPAR(tc_url);
    NGX_RTMP_SET_STRPAR(page_url);

#undef NGX_RTMP_SET_STRPAR

    s->acodecs = (uint32_t) v->acodecs;
    s->vcodecs = (uint32_t) v->vcodecs;

    object_encoding = v->object_encoding;

    if (ngx_rtmp_send_ack_size(s, cscf->ack_window) != NGX_OK ||
        ngx_rtmp_send_bandwidth(s, cscf->ack_window, NGX_RTMP_LIMIT_DYNAMIC) != NGX_OK ||
        ngx_rtmp_send_chunk_size(s, cscf->chunk_size) != NGX_OK ||
        ngx_rtmp_send_amf(s, &h, out_elts, sizeof(out_elts) / sizeof(out_elts[0])) != NGX_OK ) {

        return NGX_ERROR;
    } else {

        return ngx_rtmp_fire_event(s, NGX_RTMP_CONNECT_END, NULL, NULL);
    }
}


static ngx_int_t
ngx_rtmp_cmd_release_stream_init(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
                                ngx_chain_t *in)
{
    static ngx_rtmp_release_stream_t    v;

    static ngx_rtmp_amf_elt_t  in_elts[] = {

        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          &v.trans, sizeof(v.trans) },

        { NGX_RTMP_AMF_NULL,
          ngx_null_string,
          NULL, 0 },

        { NGX_RTMP_AMF_STRING,
          ngx_null_string,
          &v.name, sizeof(v.name) },
    };

    if (ngx_rtmp_receive_amf(s, in, in_elts,
                sizeof(in_elts) / sizeof(in_elts[0])))
    {
        return NGX_ERROR;
    }

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0, "releaseStream");

    return ngx_rtmp_release_stream(s, &v);
}


static ngx_int_t
ngx_rtmp_cmd_release_stream(ngx_rtmp_session_t *s, ngx_rtmp_release_stream_t *v)
{
    static double               trans;
    ngx_rtmp_header_t           h;

    static ngx_rtmp_amf_elt_t  out_elts[] = {

        { NGX_RTMP_AMF_STRING,
          ngx_null_string,
          "_result", 0 },

        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          &trans, 0 },

        { NGX_RTMP_AMF_NULL,
          ngx_null_string,
          NULL, 0 },

        { NGX_RTMP_AMF_ARRAY_NULL,
          ngx_null_string,
          NULL, 0 },
    };

    trans = v->trans;

    ngx_memzero(&h, sizeof(h));

    h.csid = NGX_RTMP_CSID_AMF_INI;
    h.type = NGX_RTMP_MSG_AMF_CMD;

    return ngx_rtmp_send_amf(s, &h, out_elts,
                             sizeof(out_elts) / sizeof(out_elts[0])) == NGX_OK ?
           NGX_DONE : NGX_ERROR;
}


static ngx_int_t
ngx_rtmp_cmd_create_stream_init(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
                                ngx_chain_t *in)
{
    static ngx_rtmp_create_stream_t     v;

    static ngx_rtmp_amf_elt_t  in_elts[] = {

        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          &v.trans, sizeof(v.trans) },
    };

    if (ngx_rtmp_receive_amf(s, in, in_elts,
                sizeof(in_elts) / sizeof(in_elts[0])))
    {
        return NGX_ERROR;
    }

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0, "createStream");

    return ngx_rtmp_create_stream(s, &v);
}


static ngx_int_t
ngx_rtmp_cmd_create_stream(ngx_rtmp_session_t *s, ngx_rtmp_create_stream_t *v)
{
    /* support one message stream per connection */
    static double               stream;
    static double               trans;
    ngx_rtmp_header_t           h;

    static ngx_rtmp_amf_elt_t  out_elts[] = {

        { NGX_RTMP_AMF_STRING,
          ngx_null_string,
          "_result", 0 },

        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          &trans, 0 },

        { NGX_RTMP_AMF_NULL,
          ngx_null_string,
          NULL, 0 },

        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          &stream, sizeof(stream) },
    };

    trans = v->trans;
    stream = NGX_RTMP_MSID;

    ngx_memzero(&h, sizeof(h));

    h.csid = NGX_RTMP_CSID_AMF_INI;
    h.type = NGX_RTMP_MSG_AMF_CMD;

    return ngx_rtmp_send_amf(s, &h, out_elts,
                             sizeof(out_elts) / sizeof(out_elts[0])) == NGX_OK ?
           NGX_DONE : NGX_ERROR;
}


static ngx_int_t
ngx_rtmp_cmd_close_stream_init(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
                               ngx_chain_t *in)
{
    static ngx_rtmp_close_stream_t     v;

    static ngx_rtmp_amf_elt_t  in_elts[] = {

        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          &v.stream, 0 },
    };

    if (ngx_rtmp_receive_amf(s, in, in_elts,
                             sizeof(in_elts) / sizeof(in_elts[0])))
    {
        return NGX_ERROR;
    }

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0, "closeStream");

    return ngx_rtmp_close_stream(s, &v);
}


static ngx_int_t
ngx_rtmp_cmd_close_stream(ngx_rtmp_session_t *s, ngx_rtmp_close_stream_t *v)
{
    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_cmd_delete_stream_init(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
                                ngx_chain_t *in)
{
    static ngx_rtmp_delete_stream_t     v;

    static ngx_rtmp_amf_elt_t  in_elts[] = {

        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          NULL, 0 },

        { NGX_RTMP_AMF_NULL,
          ngx_null_string,
          NULL, 0 },

        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          &v.stream, 0 },
    };

    if (ngx_rtmp_receive_amf(s, in, in_elts,
                             sizeof(in_elts) / sizeof(in_elts[0])))
    {
        return NGX_ERROR;
    }

    return ngx_rtmp_delete_stream(s, &v);
}


static ngx_int_t
ngx_rtmp_cmd_delete_stream(ngx_rtmp_session_t *s, ngx_rtmp_delete_stream_t *v)
{
    ngx_rtmp_close_stream_t         cv;

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0, "deleteStream");

    cv.stream = 0;

    return ngx_rtmp_close_stream(s, &cv);
}


static ngx_int_t
ngx_rtmp_cmd_fcpublish_init(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
                                ngx_chain_t *in)
{
    static ngx_rtmp_fcpublish_t    v;

    static ngx_rtmp_amf_elt_t  in_elts[] = {

        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          &v.trans, sizeof(v.trans) },

        { NGX_RTMP_AMF_NULL,
          ngx_null_string,
          NULL, 0 },

        { NGX_RTMP_AMF_STRING,
          ngx_null_string,
          &v.name, sizeof(v.name) },
    };

    if (ngx_rtmp_receive_amf(s, in, in_elts,
                sizeof(in_elts) / sizeof(in_elts[0])))
    {
        return NGX_ERROR;
    }

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0, "fcpublish");

    return ngx_rtmp_fcpublish(s, &v);
}


static ngx_int_t
ngx_rtmp_cmd_fcpublish(ngx_rtmp_session_t *s, ngx_rtmp_fcpublish_t *v)
{
    static double               trans;
    ngx_rtmp_header_t           h;

    static ngx_rtmp_amf_elt_t  out_elts[] = {

        { NGX_RTMP_AMF_STRING,
          ngx_null_string,
          "_result", 0 },

        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          &trans, 0 },

        { NGX_RTMP_AMF_NULL,
          ngx_null_string,
          NULL, 0 },

        { NGX_RTMP_AMF_ARRAY_NULL,
          ngx_null_string,
          NULL, 0 },
    };

    trans = v->trans;

    ngx_memzero(&h, sizeof(h));

    h.csid = NGX_RTMP_CSID_AMF_INI;
    h.type = NGX_RTMP_MSG_AMF_CMD;

    if (ngx_rtmp_send_on_fcpublish(s, &h) != NGX_OK ||
        ngx_rtmp_send_amf(s, &h, out_elts,
                          sizeof(out_elts) / sizeof(out_elts[0])) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_cmd_publish_init(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
        ngx_chain_t *in)
{
    static ngx_rtmp_publish_t       v;

    static ngx_rtmp_amf_elt_t      in_elts[] = {

        /* transaction is always 0 */
        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          NULL, 0 },

        { NGX_RTMP_AMF_NULL,
          ngx_null_string,
          NULL, 0 },

        { NGX_RTMP_AMF_STRING,
          ngx_null_string,
          &v.name, sizeof(v.name) },

        { NGX_RTMP_AMF_OPTIONAL | NGX_RTMP_AMF_STRING,
          ngx_null_string,
          &v.type, sizeof(v.type) },
    };

    ngx_memzero(&v, sizeof(v));

    if (ngx_rtmp_receive_amf(s, in, in_elts,
                             sizeof(in_elts) / sizeof(in_elts[0])))
    {
        return NGX_ERROR;
    }

    ngx_rtmp_cmd_fill_args(v.name, v.args);

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "publish: name='%s' args='%s' type=%s silent=%d",
                  v.name, v.args, v.type, v.silent);

    s->proto = NGX_PROTO_TYPE_RTMP_PUSH;
    s->host_mask |= NGX_RTMP_HOSTNAME_PUB;

    if (ngx_rtmp_cmd_get_conf(s, "publish") != NGX_OK) {

        return NGX_ERROR;
    }

    return ngx_rtmp_publish(s, &v);
}


static ngx_int_t
ngx_rtmp_cmd_publish(ngx_rtmp_session_t *s, ngx_rtmp_publish_t *v)
{
    return NGX_OK;
}


ngx_int_t
ngx_rtmp_cmd_connect_local(ngx_rtmp_session_t *s, ngx_rtmp_connect_t *v)
{
    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
            "local connect: app='%s' args='%s' flashver='%s' swf_url='%s' "
            "tc_url='%s' page_url='%s' acodecs=%uD vcodecs=%uD "
            "object_encoding=%ui",
            v->app, v->args, v->flashver, v->swf_url, v->tc_url, v->page_url,
            (uint32_t)v->acodecs, (uint32_t)v->vcodecs,
            (ngx_int_t)v->object_encoding);

    return ngx_rtmp_connect(s, v);
}


ngx_int_t
ngx_rtmp_cmd_play_local(ngx_rtmp_session_t *s, ngx_rtmp_play_t *v)
{
    if (ngx_rtmp_cmd_get_conf(s, "play local") != NGX_OK) {

        return NGX_ERROR;
    }

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                   "local play: name='%s' args='%s' start=%i duration=%i "
                   "reset=%i silent=%i",
                   v->name, v->args, (ngx_int_t) v->start,
                   (ngx_int_t) v->duration, (ngx_int_t) v->reset,
                   (ngx_int_t) v->silent);

    return ngx_rtmp_play(s, v);
}


static ngx_int_t
ngx_rtmp_cmd_play_init(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
        ngx_chain_t *in)
{
    static ngx_rtmp_play_t          v;

    static ngx_rtmp_amf_elt_t       in_elts[] = {

        /* transaction is always 0 */
        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          NULL, 0 },

        { NGX_RTMP_AMF_NULL,
          ngx_null_string,
          NULL, 0 },

        { NGX_RTMP_AMF_STRING,
          ngx_null_string,
          &v.name, sizeof(v.name) },

        { NGX_RTMP_AMF_OPTIONAL | NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          &v.start, 0 },

        { NGX_RTMP_AMF_OPTIONAL | NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          &v.duration, 0 },

        { NGX_RTMP_AMF_OPTIONAL | NGX_RTMP_AMF_BOOLEAN,
          ngx_null_string,
          &v.reset, 0 }
    };

    ngx_memzero(&v, sizeof(v));

    if (ngx_rtmp_receive_amf(s, in, in_elts,
                             sizeof(in_elts) / sizeof(in_elts[0])))
    {
        return NGX_ERROR;
    }

    ngx_rtmp_cmd_fill_args(v.name, v.args);

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "play: name='%s' args='%s' start=%i duration=%i "
                  "reset=%i silent=%i",
                  v.name, v.args, (ngx_int_t) v.start,
                  (ngx_int_t) v.duration, (ngx_int_t) v.reset,
                  (ngx_int_t) v.silent);

    s->proto = NGX_PROTO_TYPE_RTMP_PULL;
    s->host_mask |= NGX_RTMP_HOSTNAME_SUB;

    if (ngx_rtmp_cmd_get_conf(s, "play") != NGX_OK) {

        return NGX_ERROR;
    }

    return ngx_rtmp_play(s, &v);
}


static ngx_int_t
ngx_rtmp_cmd_play(ngx_rtmp_session_t *s, ngx_rtmp_play_t *v)
{
    return ngx_rtmp_fire_event(s, NGX_RTMP_PLAY_END, NULL, NULL);
}


static ngx_int_t
ngx_rtmp_cmd_play2_init(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
        ngx_chain_t *in)
{
    static ngx_rtmp_play_t          v;
    static ngx_rtmp_close_stream_t  vc;

    static ngx_rtmp_amf_elt_t       in_obj[] = {

        { NGX_RTMP_AMF_NUMBER,
          ngx_string("start"),
          &v.start, 0 },

        { NGX_RTMP_AMF_STRING,
          ngx_string("streamName"),
          &v.name, sizeof(v.name) },
    };

    static ngx_rtmp_amf_elt_t       in_elts[] = {

        /* transaction is always 0 */
        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          NULL, 0 },

        { NGX_RTMP_AMF_NULL,
          ngx_null_string,
          NULL, 0 },

        { NGX_RTMP_AMF_OBJECT,
          ngx_null_string,
          &in_obj, sizeof(in_obj) }
    };

    ngx_memzero(&v, sizeof(v));

    if (ngx_rtmp_receive_amf(s, in, in_elts,
                             sizeof(in_elts) / sizeof(in_elts[0])))
    {
        return NGX_ERROR;
    }

    ngx_rtmp_cmd_fill_args(v.name, v.args);

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "play2: name='%s' args='%s' start=%i",
                  v.name, v.args, (ngx_int_t) v.start);

    /* continue from current timestamp */

    if (v.start < 0) {
        v.start = s->current_time;
    }

    ngx_memzero(&vc, sizeof(vc));

    /* close_stream should be synchronous */
    ngx_rtmp_close_stream(s, &vc);

    return ngx_rtmp_play(s, &v);
}


static ngx_int_t
ngx_rtmp_cmd_pause_init(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
        ngx_chain_t *in)
{
    static ngx_rtmp_pause_t     v;

    static ngx_rtmp_amf_elt_t   in_elts[] = {

        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          NULL, 0 },

        { NGX_RTMP_AMF_NULL,
          ngx_null_string,
          NULL, 0 },

        { NGX_RTMP_AMF_BOOLEAN,
          ngx_null_string,
          &v.pause, 0 },

        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          &v.position, 0 },
    };

    ngx_memzero(&v, sizeof(v));

    if (ngx_rtmp_receive_amf(s, in, in_elts,
                sizeof(in_elts) / sizeof(in_elts[0])))
    {
        return NGX_ERROR;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_RTMP, s->connection->log, 0,
                   "pause: pause=%i position=%i",
                    (ngx_int_t) v.pause, (ngx_int_t) v.position);

    return ngx_rtmp_pause(s, &v);
}


static ngx_int_t
ngx_rtmp_cmd_pause(ngx_rtmp_session_t *s, ngx_rtmp_pause_t *v)
{
    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_cmd_disconnect_init(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
                        ngx_chain_t *in)
{
    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0, "disconnect");

    return ngx_rtmp_disconnect(s);
}


static ngx_int_t
ngx_rtmp_cmd_disconnect(ngx_rtmp_session_t *s)
{
    return ngx_rtmp_delete_stream(s, NULL);
}


static ngx_int_t
ngx_rtmp_cmd_seek_init(ngx_rtmp_session_t *s, ngx_rtmp_header_t *h,
        ngx_chain_t *in)
{
    static ngx_rtmp_seek_t         v;

    static ngx_rtmp_amf_elt_t      in_elts[] = {

        /* transaction is always 0 */
        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          NULL, 0 },

        { NGX_RTMP_AMF_NULL,
          ngx_null_string,
          NULL, 0 },

        { NGX_RTMP_AMF_NUMBER,
          ngx_null_string,
          &v.offset, sizeof(v.offset) },
    };

    ngx_memzero(&v, sizeof(v));

    if (ngx_rtmp_receive_amf(s, in, in_elts,
                             sizeof(in_elts) / sizeof(in_elts[0])))
    {
        return NGX_ERROR;
    }

    ngx_log_error(NGX_LOG_INFO, s->connection->log, 0,
                  "seek: offset=%i", (ngx_int_t) v.offset);

    return ngx_rtmp_seek(s, &v);
}


static ngx_int_t
ngx_rtmp_cmd_seek(ngx_rtmp_session_t *s, ngx_rtmp_seek_t *v)
{
    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_cmd_stream_begin(ngx_rtmp_session_t *s, ngx_rtmp_stream_begin_t *v)
{
    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_cmd_stream_eof(ngx_rtmp_session_t *s, ngx_rtmp_stream_eof_t *v)
{
    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_cmd_stream_dry(ngx_rtmp_session_t *s, ngx_rtmp_stream_dry_t *v)
{
    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_cmd_recorded(ngx_rtmp_session_t *s,
                      ngx_rtmp_recorded_t *v)
{
    return NGX_OK;
}


static ngx_int_t
ngx_rtmp_cmd_set_buflen(ngx_rtmp_session_t *s, ngx_rtmp_set_buflen_t *v)
{
    return NGX_OK;
}


static ngx_rtmp_amf_handler_t ngx_rtmp_cmd_map[] = {
    { ngx_string("connect"),            ngx_rtmp_cmd_connect_init           },
    { ngx_string("releaseStream"),      ngx_rtmp_cmd_release_stream_init    },
    { ngx_string("createStream"),       ngx_rtmp_cmd_create_stream_init     },
    { ngx_string("closeStream"),        ngx_rtmp_cmd_close_stream_init      },
    { ngx_string("deleteStream"),       ngx_rtmp_cmd_delete_stream_init     },
    { ngx_string("FCPublish"),          ngx_rtmp_cmd_fcpublish_init         },
    { ngx_string("publish"),            ngx_rtmp_cmd_publish_init           },
    { ngx_string("play"),               ngx_rtmp_cmd_play_init              },
    { ngx_string("play2"),              ngx_rtmp_cmd_play2_init             },
    { ngx_string("seek"),               ngx_rtmp_cmd_seek_init              },
    { ngx_string("pause"),              ngx_rtmp_cmd_pause_init             },
    { ngx_string("pauseraw"),           ngx_rtmp_cmd_pause_init             },
};


static ngx_int_t
ngx_rtmp_cmd_postconfiguration(ngx_conf_t *cf)
{
    ngx_rtmp_core_main_conf_t          *cmcf;
    ngx_rtmp_handler_pt                *h;
    ngx_rtmp_amf_handler_t             *ch, *bh;
    size_t                              n, ncalls;

    cmcf = ngx_rtmp_conf_get_module_main_conf(cf, ngx_rtmp_core_module);

    /* redirect disconnects to deleteStream
     * to free client modules from registering
     * disconnect callback */

    h = ngx_array_push(&cmcf->events[NGX_RTMP_DISCONNECT]);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_rtmp_cmd_disconnect_init;

    /* register AMF callbacks */

    ncalls = sizeof(ngx_rtmp_cmd_map) / sizeof(ngx_rtmp_cmd_map[0]);

    ch = ngx_array_push_n(&cmcf->amf, ncalls);
    if (ch == NULL) {
        return NGX_ERROR;
    }

    bh = ngx_rtmp_cmd_map;

    for(n = 0; n < ncalls; ++n, ++ch, ++bh) {
        *ch = *bh;
    }

    ngx_rtmp_connect = ngx_rtmp_cmd_connect;
    ngx_rtmp_disconnect = ngx_rtmp_cmd_disconnect;
    ngx_rtmp_release_stream = ngx_rtmp_cmd_release_stream;
    ngx_rtmp_create_stream = ngx_rtmp_cmd_create_stream;
    ngx_rtmp_close_stream = ngx_rtmp_cmd_close_stream;
    ngx_rtmp_delete_stream = ngx_rtmp_cmd_delete_stream;
    ngx_rtmp_fcpublish = ngx_rtmp_cmd_fcpublish;
    ngx_rtmp_publish = ngx_rtmp_cmd_publish;
    ngx_rtmp_play = ngx_rtmp_cmd_play;
    ngx_rtmp_seek = ngx_rtmp_cmd_seek;
    ngx_rtmp_pause = ngx_rtmp_cmd_pause;

    ngx_rtmp_stream_begin = ngx_rtmp_cmd_stream_begin;
    ngx_rtmp_stream_eof = ngx_rtmp_cmd_stream_eof;
    ngx_rtmp_stream_dry = ngx_rtmp_cmd_stream_dry;
    ngx_rtmp_recorded = ngx_rtmp_cmd_recorded;
    ngx_rtmp_set_buflen = ngx_rtmp_cmd_set_buflen;

    return NGX_OK;
}
