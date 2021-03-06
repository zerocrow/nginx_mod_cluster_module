/*
 *  mod_cluster
 *
 *  Copyright(c) 2008 Red Hat Middleware, LLC,
 *  and individual contributors as indicated by the @authors tag.
 *  See the copyright.txt in the distribution for a
 *  full listing of individual contributors. 
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library in the file COPYING.LIB;
 *  if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 *
 * @author Jean-Frederic Clere
 * @version $Revision$
 */

/**
 * @file  context.c
 * @brief context description Storage Module for Apache
 *
 * @defgroup MEM contexts
 * @ingroup  APACHE_MODS
 * @{
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#include "ngx_http_manager_module.h"
#include "../include/ngx_utils.h"


static mem_t * create_attach_mem_context(u_char *string, int *num, int type, ngx_pool_t *p, slotmem_storage_method *storage) {
    mem_t *ptr;
    const u_char *storename;
    ngx_int_t rv;

    ptr = ngx_pcalloc(p, sizeof(mem_t));
    if (!ptr) {
        return NULL;
    }
    ptr->storage =  storage;
    storename = ngx_pstrcat(p, string, CONTEXTEXE, NULL); 
    if (type)
        rv = ptr->storage->ap_slotmem_create(&ptr->slotmem, storename, sizeof(contextinfo_t), *num, type, p, storage->cf, storage->ngx_http_module);
    else {
        size_t size = sizeof(contextinfo_t);
        rv = ptr->storage->ap_slotmem_attach(&ptr->slotmem, storename, &size, num, p);
    }
    if (rv != NGX_OK) {
        return NULL;
    }
    ptr->num = *num;
    ptr->p = p;
    return ptr;
}

ngx_int_t init_mem_context(mem_t *ptr, u_char *string, int *num, ngx_pool_t *p) {
    const u_char *storename;
    ngx_int_t rv;

    if (!ptr) {
        return !NGX_OK;
    }
    
    storename = ngx_pstrcat(p, string, CONTEXTEXE, NULL); 
   
    rv = ptr->storage->ap_slotmem_init(&ptr->slotmem, storename, sizeof(contextinfo_t), *num, p);
   
    if (rv != NGX_OK) {
        return !NGX_OK;
    }
    
    return NGX_OK;
}

/**
 * Insert(alloc) and update a context record in the shared table
 * @param pointer to the shared table.
 * @param context context to store in the shared table.
 * @return NGX_OK if all went well
 *
 */
static ngx_int_t insert_update(void* mem, void **data, int id, ngx_pool_t *pool)
{
    contextinfo_t *in = (contextinfo_t *)*data;
    contextinfo_t *ou = (contextinfo_t *)mem;
    if (ngx_strcmp(in->context, ou->context) == 0 &&
               in->vhost == ou->vhost && in->node == ou->node) {
        /* We don't update nbrequests it belongs to mod_proxy_cluster logic */
        ou->status = in->status;
        ou->id = id;
        ou->updatetime = time(NULL);
        *data = ou;
        return NGX_OK;
    }
    return NGX_NONE;
}
ngx_int_t insert_update_context(mem_t *s, contextinfo_t *context)
{
    ngx_int_t rv;
    contextinfo_t *ou;
    int ident;

    context->id = 0;
    s->storage->ap_slotmem_lock(s->slotmem);
    rv = s->storage->ap_slotmem_do(s->slotmem, insert_update, &context, 1, s->p);
    if (context->id != 0 && rv == NGX_OK) {
        s->storage->ap_slotmem_unlock(s->slotmem);
        return NGX_OK; /* updated */
    }

    /* we have to insert it */
    rv = s->storage->ap_slotmem_alloc(s->slotmem, &ident, (void **) &ou);
    if (rv != NGX_OK) {
        s->storage->ap_slotmem_unlock(s->slotmem);
        return rv;
    }
    memcpy(ou, context, sizeof(contextinfo_t));
    ou->id = ident;
    ou->nbrequests = 0;
    s->storage->ap_slotmem_unlock(s->slotmem);
    ou->updatetime = time(NULL);

    return NGX_OK;
}

/**
 * read a context record from the shared table
 * @param pointer to the shared table.
 * @param context context to read from the shared table.
 * @return address of the read context or NULL if error.
 */
static ngx_int_t loc_read_context(void* mem, void **data, int id, ngx_pool_t *pool) {
    contextinfo_t *in = (contextinfo_t *)*data;
    contextinfo_t *ou = (contextinfo_t *)mem;
    if (strcmp(in->context, ou->context) == 0 &&
        in->vhost == ou->vhost && ou->node == in->node) {
        *data = ou;
        return NGX_OK;
    }
    return NGX_NONE;
}
contextinfo_t * read_context(mem_t *s, contextinfo_t *context)
{
    ngx_int_t rv;
    contextinfo_t *ou = context;

    if (context->id)
        rv = s->storage->ap_slotmem_mem(s->slotmem, context->id, (void **) &ou);
    else {
        rv = s->storage->ap_slotmem_do(s->slotmem, loc_read_context, &ou, 0, s->p);
    }
    if (rv == NGX_OK)
        return ou;
    return NULL;
}
/**
 * get a context record from the shared table
 * @param pointer to the shared table.
 * @param context address where the context is locate in the shared table.
 * @param ids  in the context table.
 * @return NGX_OK if all went well
 */
ngx_int_t get_context(mem_t *s, contextinfo_t **context, int ids)
{
  return(s->storage->ap_slotmem_mem(s->slotmem, ids, (void **) context));
}

/**
 * remove(free) a context record from the shared table
 * @param pointer to the shared table.
 * @param context context to remove from the shared table.
 * @return NGX_OK if all went well
 */
ngx_int_t remove_context(mem_t *s, contextinfo_t *context)
{
    ngx_int_t rv;
    contextinfo_t *ou = context;
    if (context->id)
        rv = s->storage->ap_slotmem_free(s->slotmem, context->id, context);
    else {
        /* XXX: for the moment January 2007 ap_slotmem_free only uses ident to remove */
        rv = s->storage->ap_slotmem_do(s->slotmem, loc_read_context, &ou, 0, s->p);
        if (rv == NGX_OK)
            rv = s->storage->ap_slotmem_free(s->slotmem, ou->id, context);
    }
    return rv;
}

/*
 * lock the context table
 * @param pointer to the shared table.
 */
void lock_contexts(mem_t *s)
{
    s->storage->ap_slotmem_lock(s->slotmem);
}

/*
 * unlock the context table
 * @param pointer to the shared table.
 */
void unlock_contexts(mem_t *s)
{
    s->storage->ap_slotmem_unlock(s->slotmem);
}

/*
 * get the ids for the used (not free) contexts in the table
 * @param pointer to the shared table.
 * @param ids array of int to store the used id (must be big enough).
 * @return number of context existing or -1 if error.
 */
int get_ids_used_context(mem_t *s, int *ids)
{
    return (s->storage->ap_slotmem_get_used(s->slotmem, ids));
}

/*
 * read the size of the table.
 * @param pointer to the shared table.
 * @return number of context existing or -1 if error.
 */
int get_max_size_context(mem_t *s)
{
    return (s->storage->ap_slotmem_get_max_size(s->slotmem));
}

/*
 * read the version of the table.
 * @param pointer to the shared table.
 * @return the version of the table
 */
unsigned int get_version_context(mem_t *s)
{
    if (s->storage == NULL)
        return 0;
    else
        return (s->storage->ap_slotmem_get_version(s->slotmem));
}

/**
 * attach to the shared context table
 * @param name of an existing shared table.
 * @param address to store the size of the shared table.
 * @param p pool to use for allocations.
 * @param storage slotmem logic provider.
 * @return address of struct used to access the table.
 */
mem_t * get_mem_context(u_char *string, int *num, ngx_pool_t *p, slotmem_storage_method *storage)
{
    return(create_attach_mem_context(string, num, 0, p, storage));
}
/**
 * create a shared context table
 * @param name to use to create the table.
 * @param size of the shared table.
 * @param persist tell if the slotmem element are persistent.
 * @param p pool to use for allocations.
 * @param storage slotmem logic provider.
 * @return address of struct used to access the table.
 */
mem_t * create_mem_context(u_char *string, int *num, int persist, ngx_pool_t *p, slotmem_storage_method *storage)
{
    return(create_attach_mem_context(string, num, CREATE_SLOTMEM|persist, p, storage));
}
