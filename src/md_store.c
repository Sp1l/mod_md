/* Copyright 2017 greenbytes GmbH (https://www.greenbytes.de)
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include <apr_lib.h>
#include <apr_file_info.h>
#include <apr_file_io.h>
#include <apr_fnmatch.h>
#include <apr_hash.h>
#include <apr_strings.h>

#include "md.h"
#include "md_crypt.h"
#include "md_log.h"
#include "md_json.h"
#include "md_store.h"
#include "md_util.h"

/**************************************************************************************************/
/* generic callback handling */

#define ASPECT_MD           "md.json"
#define ASPECT_CERT         "cert.pem"
#define ASPECT_PKEY         "key.pem"
#define ASPECT_CHAIN        "chain.pem"

#define GNAME_ACCOUNTS     "accounts"
#define GNAME_CHALLENGES   "challenges"
#define GNAME_DOMAINS      "domains"
#define GNAME_STAGING      "staging"
#define GNAME_ARCHIVE      "archive"

static const char *GROUP_FNAME[] = {
    GNAME_ACCOUNTS,
    GNAME_CHALLENGES,
    GNAME_DOMAINS,
    GNAME_STAGING,
    GNAME_ARCHIVE,
};

const char *md_store_group_name(int group)
{
    if (group < sizeof(GROUP_FNAME)/sizeof(GROUP_FNAME[0])) {
        return GROUP_FNAME[group];
    }
    return "UNKNOWN";
}

void md_store_destroy(md_store_t *store)
{
    if (store->destroy) store->destroy(store);
}

apr_status_t md_store_load(md_store_t *store, md_store_group_t group, 
                           const char *name, const char *aspect, 
                           md_store_vtype_t vtype, void **pdata, 
                           apr_pool_t *p)
{
    return store->load(store, group, name, aspect, vtype, pdata, p);
}

apr_status_t md_store_save(md_store_t *store, md_store_group_t group, 
                           const char *name, const char *aspect, 
                           md_store_vtype_t vtype, void *data, 
                           int create)
{
    return store->save(store, group, name, aspect, vtype, data, create);
}

apr_status_t md_store_remove(md_store_t *store, md_store_group_t group, 
                             const char *name, const char *aspect, 
                             apr_pool_t *p, int force)
{
    return store->remove(store, group, name, aspect, p, force);
}

apr_status_t md_store_purge(md_store_t *store, md_store_group_t group, 
                             const char *name)
{
    return store->purge(store, group, name);
}

apr_status_t md_store_iter(md_store_inspect *inspect, void *baton, md_store_t *store, 
                           md_store_group_t group, const char *pattern, const char *aspect,
                           md_store_vtype_t vtype)
{
    return store->iterate(inspect, baton, store, group, pattern, aspect, vtype);
}

apr_status_t md_store_load_json(md_store_t *store, md_store_group_t group, 
                                const char *name, const char *aspect, 
                                struct md_json_t **pdata, apr_pool_t *p)
{
    return md_store_load(store, group, name, aspect, MD_SV_JSON, (void**)pdata, p);
}

apr_status_t md_store_save_json(md_store_t *store, md_store_group_t group, 
                                const char *name, const char *aspect, 
                                struct md_json_t *data, int create)
{
    return md_store_save(store, group, name, aspect, MD_SV_JSON, (void*)data, create);
}

apr_status_t md_store_move(md_store_t *store, md_store_group_t from, md_store_group_t to,
                           const char *name, int archive)
{
    return store->move(store, from, to, name, archive);
}

apr_status_t md_store_get_fname(const char **pfname, 
                                md_store_t *store, md_store_group_t group, 
                                const char *name, const char *aspect, 
                                apr_pool_t *p)
{
    if (store->get_fname) {
        return store->get_fname(pfname, store, group, name, aspect, p);
    }
    return APR_ENOTIMPL;
}

/**************************************************************************************************/
/* convenience */

typedef struct {
    md_store_t *store;
    md_store_group_t group;
} md_group_ctx;

apr_status_t md_load(md_store_t *store, md_store_group_t group, 
                     const char *name, md_t **pmd, apr_pool_t *p)
{
    md_json_t *json;
    apr_status_t rv;
    
    rv = md_store_load_json(store, group, name, MD_FN_MD, &json, p);
    if (APR_SUCCESS == rv) {
        *pmd = md_from_json(json, p);
        return APR_SUCCESS;
    }
    return rv;
}

static apr_status_t p_save(void *baton, apr_pool_t *p, apr_pool_t *ptemp, va_list ap)
{
    md_group_ctx *ctx = baton;
    md_json_t *json;
    md_t *md;
    int create;
    
    md = va_arg(ap, md_t *);
    create = va_arg(ap, int);

    json = md_to_json(md, ptemp);
    assert(json);
    assert(md->name);
    return md_store_save_json(ctx->store, ctx->group, md->name, MD_FN_MD, json, create);
}

apr_status_t md_save(md_store_t *store, md_store_group_t group, md_t *md, int create)
{
    md_group_ctx ctx;
    
    ctx.store = store;
    ctx.group = group;
    return md_util_pool_vdo(p_save, &ctx, store->p, md, create, NULL);
}

static apr_status_t p_remove(void *baton, apr_pool_t *p, apr_pool_t *ptemp, va_list ap)
{
    md_group_ctx *ctx = baton;
    const char *name;
    int force;
    
    name = va_arg(ap, const char *);
    force = va_arg(ap, int);

    assert(name);
    return md_store_remove(ctx->store, ctx->group, name, MD_FN_MD, ptemp, force);
}

apr_status_t md_remove(md_store_t *store, md_store_group_t group, const char *name, int force)
{
    md_group_ctx ctx;
    
    ctx.store = store;
    ctx.group = group;
    return md_util_pool_vdo(p_remove, &ctx, store->p, name, force, NULL);
}

typedef struct {
    apr_pool_t *p;
    apr_array_header_t *mds;
} md_load_ctx;

apr_status_t md_pkey_load(md_store_t *store, md_store_group_t group, const char *name, 
                          md_pkey_t **ppkey, apr_pool_t *p)
{
    return md_store_load(store, group, name, MD_FN_PKEY, MD_SV_PKEY, (void**)ppkey, p);
}

apr_status_t md_pkey_save(md_store_t *store, md_store_group_t group, const char *name, 
                          struct md_pkey_t *pkey, int create)
{
    return md_store_save(store, group, name, MD_FN_PKEY, MD_SV_PKEY, pkey, create);
}

apr_status_t md_cert_load(md_store_t *store, md_store_group_t group, const char *name, 
                          struct md_cert_t **pcert, apr_pool_t *p)
{
    return md_store_load(store, group, name, MD_FN_CERT, MD_SV_CERT, (void**)pcert, p);
}

apr_status_t md_cert_save(md_store_t *store, md_store_group_t group, const char *name, 
                          struct md_cert_t *cert, int create)
{
    return md_store_save(store, group, name, MD_FN_CERT, MD_SV_CERT, cert, create);
}

apr_status_t md_chain_load(md_store_t *store, md_store_group_t group, const char *name, 
                           struct apr_array_header_t **pchain, apr_pool_t *p)
{
    return md_store_load(store, group, name, MD_FN_CHAIN, MD_SV_CHAIN, (void**)pchain, p);
}

apr_status_t md_chain_save(md_store_t *store, md_store_group_t group, const char *name, 
                           struct apr_array_header_t *chain, int create)
{
    return md_store_save(store, group, name, MD_FN_CHAIN, MD_SV_CHAIN, chain, create);
}

typedef struct {
    md_store_t *store;
    md_store_group_t group;
    const char *pattern;
    const char *aspect;
    md_store_md_inspect *inspect;
    void *baton;
} inspect_md_ctx;

static int insp_md(void *baton, const char *name, const char *aspect, 
                   md_store_vtype_t vtype, void *value, apr_pool_t *ptemp)
{
    inspect_md_ctx *ctx = baton;
    
    if (!strcmp(MD_FN_MD, aspect) && vtype == MD_SV_JSON) {
        const md_t *md = md_from_json(value, ptemp);
        md_log_perror(MD_LOG_MARK, MD_LOG_TRACE3, 0, ptemp, "inspecting md at: %s", name);
        return ctx->inspect(ctx->baton, ctx->store, md, ptemp);
    }
    return 1;
}

apr_status_t md_store_md_iter(md_store_md_inspect *inspect, void *baton, md_store_t *store, 
                              md_store_group_t group, const char *pattern)
{
    inspect_md_ctx ctx;
    
    ctx.store = store;
    ctx.group = group;
    ctx.inspect = inspect;
    ctx.baton = baton;
    
    return md_store_iter(insp_md, &ctx, store, group, pattern, MD_FN_MD, MD_SV_JSON);
}
