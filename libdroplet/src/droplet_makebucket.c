/*
 * Droplet, high performance cloud storage client library
 * Copyright (C) 2010 Scality http://github.com/scality/Droplet
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *  
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "dropletp.h"

//#define DPRINTF(fmt,...) fprintf(stderr, fmt, ##__VA_ARGS__)
#define DPRINTF(fmt,...)

dpl_location_constraint_t 
dpl_location_constraint(char *str)
{
  if (!strcasecmp(str, ""))
    return DPL_LOCATION_CONSTRAINT_US_STANDARD;
  else if (!strcasecmp(str, "EU"))
    return DPL_LOCATION_CONSTRAINT_EU;
  else if (!strcasecmp(str, "us-west-1"))
    return DPL_LOCATION_CONSTRAINT_US_WEST_1;
  else if (!strcasecmp(str, "ap-southeast-1"))
    return DPL_LOCATION_CONSTRAINT_AP_SOUTHEAST_1;

  return -1;
}

char *
dpl_location_constraint_str(dpl_location_constraint_t location_constraint)
{
  switch (location_constraint)
    {
    case DPL_LOCATION_CONSTRAINT_US_STANDARD:
      return "";
    case DPL_LOCATION_CONSTRAINT_EU:
      return "EU";
    case DPL_LOCATION_CONSTRAINT_US_WEST_1:
      return "us-west-1";
    case DPL_LOCATION_CONSTRAINT_AP_SOUTHEAST_1:
      return "ap-southeast-1";
    }
  
  return NULL;
}

/** 
 * make a bucket
 * 
 * @param ctx 
 * @param bucket 
 * @param location_constraint 
 * @param canned_acl 
 * 
 * @return 
 */
dpl_status_t
dpl_make_bucket(dpl_ctx_t *ctx,
                char *bucket,
                dpl_location_constraint_t location_constraint,
                dpl_canned_acl_t canned_acl)
{
  char          host[1024];
  char          data_len_str[64];
  int           ret, ret2;
  struct hostent hret, *hresult;
  char          hbuf[1024];
  int           herr;
  struct in_addr addr;
  dpl_conn_t    *conn = NULL;
  char          header[1024];
  u_int         header_len;
  struct iovec  iov[10];
  int           n_iov = 0;
  int           connection_close = 0;
  char          *acl_str;
  dpl_dict_t    *headers = NULL;
  char          *location_constraint_str;
  char          data_str[1024];
  u_int         data_len;
  dpl_dict_t    *headers_returned = NULL;

  snprintf(host, sizeof (host), "%s.%s", bucket, ctx->host);

  DPRINTF("dpl_make_bucket: host=%s:%d\n", host, ctx->port);

  headers = dpl_dict_new(13);
  if (NULL == headers)
    {
      ret = DPL_ENOMEM;
      goto end;
    }

  ret2 = dpl_dict_add(headers, "Host", host, 0);
  if (DPL_SUCCESS != ret2)
    {
      ret = DPL_ENOMEM;
      goto end;
    }

  ret2 = dpl_dict_add(headers, "Connection", "keep-alive", 0);
  if (DPL_SUCCESS != ret2)
    {
      ret = DPL_ENOMEM;
      goto end;
    }

  if (DPL_LOCATION_CONSTRAINT_US_STANDARD == location_constraint)
    {
      data_str[0] = 0;
      data_len = 0;
    }
  else
    {
      location_constraint_str = dpl_location_constraint_str(location_constraint);
      if (NULL == location_constraint_str)
        {
          ret = DPL_ENOMEM;
          goto end;
        }
      
      snprintf(data_str, sizeof (data_str), 
               "<CreateBucketConfiguration xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\">\n"
               "<LocationConstraint>%s</LocationConstraint>\n"
               "</CreateBucketConfiguration>\n",
               location_constraint_str);
      
      data_len = strlen(data_str);
    }

  snprintf(data_len_str, sizeof (data_len_str), "%u", data_len);
  ret2 = dpl_dict_add(headers, "Content-Length", data_len_str, 0);
  if (DPL_SUCCESS != ret2)
    {
      ret = DPL_ENOMEM; 
      goto end;
    }

  acl_str = dpl_canned_acl_str(canned_acl);
  if (NULL == acl_str)
    {
      ret = DPL_ENOMEM;
      goto end;
    }

  ret2 = dpl_dict_add(headers, "x-amz-acl", acl_str, 0);
  if (DPL_SUCCESS != ret2)
    {
      ret = DPL_ENOMEM;
      goto end;
    }

  ret2 = linux_gethostbyname_r(host, &hret, hbuf, sizeof (hbuf), &hresult, &herr); 
  if (0 != ret2)
    {
      ret = DPL_FAILURE;
      goto end;
    }

  if (AF_INET != hresult->h_addrtype)
    {
      ret = DPL_FAILURE;
      goto end;
    }

  memcpy(&addr, hresult->h_addr_list[0], hresult->h_length);

  conn = dpl_conn_open(ctx, addr, ctx->port);
  if (NULL == conn)
    {
      ret = DPL_FAILURE;
      goto end;
    }

  //build request
  ret2 = dpl_build_s3_request(ctx, 
                              "PUT",
                              bucket,
                              "/",
                              NULL,
                              NULL,
                              headers, 
                              header,
                              sizeof (header),
                              &header_len);
  if (DPL_SUCCESS != ret2)
    {
      ret = DPL_FAILURE;
      goto end;
    }

  iov[n_iov].iov_base = header;
  iov[n_iov].iov_len = header_len;
  n_iov++;

  //final crlf
  iov[n_iov].iov_base = "\r\n";
  iov[n_iov].iov_len = 2;
  n_iov++;

  //buffer
  iov[n_iov].iov_base = data_str;
  iov[n_iov].iov_len = data_len;
  n_iov++;
  
  ret2 = dpl_conn_writev_all(conn, iov, n_iov, conn->ctx->write_timeout);
  if (DPL_SUCCESS != ret2)
    {
      DPLERR(1, "writev failed");
      connection_close = 1;
      ret = DPL_ENOENT; //mapped to 404
      goto end;
    }

  ret2 = dpl_read_http_reply(conn, NULL, NULL, &headers_returned);
  if (DPL_SUCCESS != ret2)
    {
      if (DPL_ENOENT == ret2)
        {
          ret = DPL_ENOENT;
          goto end;
        }
      else
        {
          DPLERR(0, "read http answer failed");
          connection_close = 1;
          ret = DPL_ENOENT; //mapped to 404
          goto end;
        }
    }
  else
    {
      connection_close = dpl_connection_close(headers_returned);
    }

  (void) dpl_log_charged_event(ctx, "DATA", "IN", data_len);

  ret = DPL_SUCCESS;

 end:

  if (NULL != conn)
    {
      if (1 == connection_close)
        dpl_conn_terminate(conn);
      else
        dpl_conn_release(conn);
    }

  if (NULL != headers)
    dpl_dict_free(headers);

  if (NULL != headers_returned)
    dpl_dict_free(headers_returned);

  DPRINTF("ret=%d\n", ret);

  return ret;
}