/*
 * entries.c :  manipulating the administrative `entries' file.
 *
 * ================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * 3. The end-user documentation included with the redistribution, if
 * any, must include the following acknowlegement: "This product includes
 * software developed by CollabNet (http://www.Collab.Net)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of CollabNet.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLABNET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by many
 * individuals on behalf of CollabNet.
 */



#include <apr_strings.h>
#include <assert.h>
#include "wc.h"
#include "svn_xml.h"
#include "svn_error.h"
#include "svn_types.h"


/*------------------------------------------------------------------*/

/** Overview **/

/* The administrative `entries' file tracks information about files
   and subdirs within a particular directory.
   
   See the section on the `entries' file in libsvn_wc/README, for
   concrete information about the XML format.
*/


/*--------------------------------------------------------------- */

/*** Initialization of the entries file. ***/

svn_error_t *
svn_wc__entries_init (svn_string_t *path, apr_pool_t *pool)
{
  svn_error_t *err;
  apr_status_t apr_err;
  apr_file_t *f = NULL;
  svn_string_t *accum;
  char *initial_verstr = apr_psprintf (pool, "%ld", 0);

  /* Create the entries file, which must not exist prior to this. */
  err = svn_wc__open_adm_file (&f, path, SVN_WC__ADM_ENTRIES,
                               (APR_WRITE | APR_CREATE | APR_EXCL), pool);
  if (err)
    return err;

  /* Make a the XML standard header, to satisfy bureacracy. */
  accum = svn_xml_make_header (pool);

  /* Open the file's top-level form. */
  svn_xml_append_tag (accum,
                      pool,
                      svn_xml_open_tag,
                      SVN_WC__ENTRIES_START,
                      "xmlns",
                      svn_string_create (SVN_XML_NAMESPACE, pool),
                      NULL);

  /* Add an entry for the dir itself -- name is absent, only the
     version is present in the dir entry. */
  svn_xml_append_tag (accum,
                      pool,
                      svn_xml_self_close_tag,
                      SVN_WC__ENTRIES_ENTRY,
                      SVN_WC__ENTRIES_ATTR_VERSION,
                      svn_string_create (initial_verstr, pool),
                      NULL);

  /* Close the top-level form. */
  svn_xml_append_tag (accum,
                      pool,
                      svn_xml_close_tag,
                      SVN_WC__ENTRIES_END,
                      NULL);

  apr_err = apr_full_write (f, accum->data, accum->len, NULL);
  if (apr_err)
    {
      apr_close (f);
      return svn_error_createf (apr_err, 0, NULL, pool,
                                "svn_wc__entries_init: "
                                "error writing %s's entries file",
                                path->data);
    }

  /* Now we have a `entries' file with exactly one entry, an entry
     for this dir.  Close the file and sync it up. */
  err = svn_wc__close_adm_file (f, path, SVN_WC__ADM_ENTRIES, 1, pool);
  if (err)
    return err;

  return SVN_NO_ERROR;
}


/*--------------------------------------------------------------- */

/*** ancestry ***/
svn_error_t *
svn_wc__entry_get_ancestry (svn_string_t *path,
                            svn_string_t *entry,
                            svn_string_t **ancestor_path,
                            svn_vernum_t *ancestor_ver,
                            apr_pool_t *pool)
{
  svn_string_t *ancestor;
  svn_vernum_t version;
  svn_error_t *err;
  apr_hash_t *h;

  err = svn_wc__entry_get (path, entry, &version, NULL, pool, &h);
  if (err)
    return err;

  ancestor = apr_hash_get (h,
                           SVN_WC__ENTRIES_ATTR_ANCESTOR,
                           strlen (SVN_WC__ENTRIES_ATTR_ANCESTOR));

  /* Default back to directory itself, in case anything's missing. */
  if (entry && ((! ancestor) || (version == SVN_INVALID_VERNUM)))
    {
      svn_string_t *default_ancestor;
      svn_vernum_t default_version;
      
      err = svn_wc__entry_get (path,
                               NULL,
                               &default_version,
                               NULL,
                               pool,
                               &h);
      if (err)
        return err;
      
      if (! ancestor)
        {
          ancestor = default_ancestor;
          svn_path_add_component (ancestor,
                                  entry,
                                  svn_path_repos_style,
                                  pool);
        }
      
      if (version == SVN_INVALID_VERNUM)
        version = default_version;
    }

  /* If we still don't have the information, that's an error. */
  if ((! ancestor) || (version == SVN_INVALID_VERNUM))
    return svn_error_createf (SVN_ERR_WC_ENTRY_MISSING_ANCESTRY,
                              0,
                              NULL,
                              pool,
                              "missing ancestor path or version for %s/%s",
                              path->data,
                              entry ? entry->data : "");


  *ancestor_path = ancestor;
  *ancestor_ver  = version;

  return SVN_NO_ERROR;
}



/*--------------------------------------------------------------- */

/*** xml callbacks ***/

/* For a given ENTRYNAME in PATH's entries file, set the entry's
 * version to VERSION.  Also set other XML attributes via varargs:
 * key, value, key, value, etc, terminated by a single NULL.  (The
 * keys are char *'s and values are svn_string_t *'s.)
 * 
 * If no such ENTRYNAME exists, create it.
 */


/* The userdata that will go to our expat callbacks */
typedef struct svn_wc__entry_baton_t
{
  apr_pool_t *pool;
  svn_xml_parser_t *parser;

  svn_boolean_t found_it;  /* Gets set to true iff we see a matching entry. */

  svn_boolean_t removing;  /* Set iff the task is to remove an entry. */

  apr_file_t *infile;      /* The entries file we're reading from. */
  apr_file_t *outfile;     /* If this is NULL, then we're GETTING
                              attributes; if this is non-NULL, then
                              we're SETTING attributes by writing a
                              new file.  */

  svn_string_t *entryname; /* The name of the entry we're looking for. */
  svn_vernum_t version;    /* The version we will get or set. */
  enum svn_node_kind kind; /* The kind we will get or set. */

  apr_hash_t *attributes;  /* The attribute list from XML, which will
                              be read from and written to. */

} svn_wc__entry_baton_t;



/* Search through ATTS and fill in DESIRED_ATTRS appropriately.
   DESIRED_ATTRS is a hash whose keys are char *'s and values are
   svn_string_t **'s, which will be set to point to new strings
   representing the values discovered in ATTS.

   Certain arguments will also be converted and set directly: VERSION
   and KIND.
 */
static void
get_entry_attributes (const char **atts,
                      svn_vernum_t *version,
                      enum svn_node_kind *kind,
                      apr_hash_t *desired_attrs,
                      apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  const char *found_version, *found_kind;

  /* Handle version specially. */
  found_version = svn_xml_get_attr_value (SVN_WC__ENTRIES_ATTR_VERSION, atts);
  if (found_version)
    *version = (svn_vernum_t) atoi (found_version);
  else
    *version = SVN_INVALID_VERNUM;
      
  /* Handle kind specially. */
  found_kind = svn_xml_get_attr_value (SVN_WC__ENTRIES_ATTR_KIND, atts);
  *kind = 0;  /* set to a known invalid default */
  if (found_kind)
    {
      if (strcmp (found_kind, "file") == 0)
        *kind = svn_file_kind;
      else if (strcmp (found_kind, "dir") == 0)
        *kind = svn_dir_kind;
      /* someday there will be symlink kind, etc, here */
    }

  /* Now loop through the other requested attributes, setting by reference. */
  for (hi = apr_hash_first (desired_attrs); hi; hi = apr_hash_next (hi))
    {
      const char *key;
      size_t keylen;
      const char *val; 
      svn_string_t **receiver;
      
      apr_hash_this (hi, (const void **) &key, &keylen, (void **) &receiver);
      assert (receiver != NULL);

      val = svn_xml_get_attr_value (key, atts);
      *receiver = (val ? svn_string_create (val, pool) : NULL);
    }
}


static svn_error_t *
write_entry (apr_file_t *outfile,
             svn_string_t *entryname,
             svn_vernum_t version,
             enum svn_node_kind kind,
             apr_hash_t *attributes,
             apr_pool_t *pool)
{
  apr_status_t apr_err;
  svn_string_t *entry;
  svn_string_t *verstr
    = svn_string_create (apr_psprintf (pool, "%ld", (long int) version), pool);
  svn_string_t *kindstr;

  switch (kind)
    {
    case svn_file_kind:
      kindstr = svn_string_create ("file", pool);
      break;
    case svn_dir_kind:
      kindstr = svn_string_create ("dir", pool);
      break;
    default:
      kindstr = NULL;  /* tolerate unknown kind, for forward compatibility */
    }
  
  apr_hash_set (attributes,
                SVN_WC__ENTRIES_ATTR_NAME,
                strlen (SVN_WC__ENTRIES_ATTR_NAME),
                entryname);

  apr_hash_set (attributes,
                SVN_WC__ENTRIES_ATTR_VERSION,
                strlen (SVN_WC__ENTRIES_ATTR_VERSION),
                verstr);

  if (kind)
    apr_hash_set (attributes,
                  SVN_WC__ENTRIES_ATTR_KIND,
                  strlen (SVN_WC__ENTRIES_ATTR_KIND),
                  kindstr);
  
  entry = svn_xml_make_tag_hash (pool,
                                 svn_xml_self_close_tag,
                                 SVN_WC__ENTRIES_ENTRY,
                                 attributes);

  apr_err = apr_full_write (outfile, entry->data, entry->len, NULL);
  if (apr_err)
    return svn_error_create (apr_err, 0, NULL, pool, "write_entry");

  return SVN_NO_ERROR;
}


/* Called whenever we find an <open> tag of some kind. */
static void
handle_start_tag (void *userData, const char *tagname, const char **atts)
{
  svn_wc__entry_baton_t *baton = (svn_wc__entry_baton_t *) userData;
  svn_error_t *err;

  /* We only care about the `entry' tag; all other tags, such as `xml'
     and `wc-entries', are simply written back out verbatim. */

  if ((strcmp (tagname, SVN_WC__ENTRIES_ENTRY)) == 0)
    {
      const char *entry
        = svn_xml_get_attr_value (SVN_WC__ENTRIES_ATTR_NAME, atts);
      
      /* Nulls count as a match, because null represents the dir itself. */
      if (((entry == NULL) && (baton->entryname == NULL))
          || ((entry != NULL)
              && (baton->entryname != NULL)
              && ((strcmp (entry, baton->entryname->data)) == 0)))
        {
          baton->found_it = 1;

          if (baton->outfile) /* we're writing out a change */
            {
              /* Rewrite the tag only if we're not removing it. */
              if (! baton->removing)
                {
                  err = write_entry (baton->outfile,
                                     baton->entryname,
                                     baton->version,
                                     baton->kind,
                                     baton->attributes,
                                     baton->pool);
                  if (err)
                    {
                      svn_xml_signal_bailout (err, baton->parser);
                      return;
                    }
                }
            }
          else  /* just reading attribute values, not writing a new tag */
            get_entry_attributes (atts,
                                  &(baton->version),
                                  &(baton->kind),
                                  baton->attributes,
                                  baton->pool);
        }
      else  /* An entry tag, but not the one we're looking for. */
        goto write_it_back_out;
    }
  else  /* This is some tag other than `entry', preserve it unchanged.  */
    {
    write_it_back_out:
      if (baton->outfile)
        {
          enum svn_xml_tag_type tag_type = svn_xml_self_close_tag;
          apr_status_t apr_err;
          svn_string_t *dup;

          if (strcmp (tagname, SVN_WC__ENTRIES_START) == 0)
            tag_type = svn_xml_open_tag;
          else if (strcmp (tagname, SVN_WC__ENTRIES_END) == 0)
            tag_type = svn_xml_close_tag;

          dup = svn_xml_make_tag_hash (baton->pool,
                                       tag_type,
                                       tagname,
                                       svn_xml_make_att_hash
                                       (atts, baton->pool));
          
          apr_err = apr_full_write (baton->outfile, dup->data, dup->len, NULL);
          if (apr_err)
            {
              err = svn_error_create (apr_err, 0, NULL, baton->pool,
                                      "entries.c: handle_start_tag");
              svn_xml_signal_bailout (err, baton->parser);
              return;
            }
        }
    }
}


/* Called whenever we find a </close> tag of some kind. */
static void
handle_end_tag (void *userData, const char *tagname)
{
  svn_wc__entry_baton_t *baton = (svn_wc__entry_baton_t *) userData;
  svn_error_t *err;

  if ((strcmp (tagname, SVN_WC__ENTRIES_END)) == 0)
    {
      if (baton->outfile)
        {
          apr_status_t apr_err;
          svn_string_t *close_tag;

          /* If this entry didn't exist before, then add it now. */
          if (! baton->found_it)
            {
              err = write_entry (baton->outfile,
                                 baton->entryname,
                                 baton->version,
                                 baton->kind,
                                 baton->attributes,
                                 baton->pool);
              if (err)
                {
                  svn_xml_signal_bailout (err, baton->parser);
                  return;
                }
            }

          /* Now close off the file. */
          close_tag = svn_xml_make_tag (baton->pool,
                                        svn_xml_close_tag,
                                        tagname,
                                        NULL);

          apr_err = apr_full_write (baton->outfile,
                                    close_tag->data,
                                    close_tag->len,
                                    NULL);
          if (apr_err)
            {
              err = svn_error_create (apr_err, 0, NULL, baton->pool,
                                      "entries.c: handle_end_tag");
              svn_xml_signal_bailout (err, baton->parser);
            }
        }
    }
}


/* Code chunk shared by svn_wc__{get,set}_entry()
   
   Parses xml in BATON->infile using BATON as userdata. */
static svn_error_t *
do_parse (svn_wc__entry_baton_t *baton)
{
  svn_error_t *err;
  svn_xml_parser_t *svn_parser;
  char buf[BUFSIZ];
  apr_status_t apr_err;
  apr_size_t bytes_read;

  /* Create a custom XML parser */
  svn_parser = svn_xml_make_parser (baton,
                                    handle_start_tag,
                                    handle_end_tag,
                                    NULL,
                                    baton->pool);

  baton->parser = svn_parser;  /* Don't forget to store the parser in
                                  our userdata, so that callbacks can
                                  call svn_xml_signal_bailout() */

  /* Parse the xml in infile, and write modified stream back out to
     outfile. */
  do {
    apr_err = apr_full_read (baton->infile, buf, BUFSIZ, &bytes_read);
    if (apr_err && (apr_err != APR_EOF))
      return svn_error_create 
        (apr_err, 0, NULL, baton->pool,
         "svn_wc__entry_set: apr_full_read choked");
    
    err = svn_xml_parse (svn_parser, buf, bytes_read, (apr_err == APR_EOF));
    if (err)
      return svn_error_quick_wrap 
        (err,
         "svn_wc__entry_set:  xml parser failed.");
  } while (apr_err != APR_EOF);


  /* Clean up xml parser */
  svn_xml_free_parser (svn_parser);

  return SVN_NO_ERROR;
}



/*----------------------------------------------------------------------*/

/*** Getting and setting entries. ***/

/* Common code for entry_set and entry_get. */
static svn_error_t *
do_entry (svn_string_t *path,
          apr_pool_t *pool,
          svn_string_t *entryname,
          svn_vernum_t version,
          svn_vernum_t *version_receiver,
          enum svn_node_kind kind,
          enum svn_node_kind *kind_receiver,
          svn_boolean_t removing,
          svn_boolean_t setting,
          apr_hash_t *attributes)
{
  svn_error_t *err;
  apr_file_t *infile = NULL;
  apr_file_t *outfile = NULL;

  svn_wc__entry_baton_t *baton 
    = apr_pcalloc (pool, sizeof (svn_wc__entry_baton_t));

  assert (! (setting && removing));
  assert (! ((entryname == NULL) && removing));

  /* Open current entries file for reading */
  err = svn_wc__open_adm_file (&infile, path,
                               SVN_WC__ADM_ENTRIES,
                               APR_READ, pool);
  if (err)
    return err;

  if (setting || removing)
    {
      apr_status_t apr_err;
      svn_string_t *front;

      /* Open a new `tmp/entries' file for writing */
      err = svn_wc__open_adm_file (&outfile, path,
                                   SVN_WC__ADM_ENTRIES,
                                   (APR_WRITE | APR_CREATE | APR_EXCL), pool);
      if (err)
        return err;

      front = svn_xml_make_header (pool);
      apr_err = apr_full_write (outfile, front->data, front->len, NULL);
      if (apr_err)
        {
          apr_close (outfile);
          return svn_error_create (apr_err, 0, NULL, baton->pool,
                                   "entries.c: do_entry");
        }
    }

  /* Fill in the userdata structure */
  baton->pool       = pool;
  baton->infile     = infile;
  baton->outfile    = outfile;
  baton->removing   = removing;
  baton->entryname  = entryname;
  baton->version    = version;
  baton->kind       = kind;
  baton->attributes = attributes;

  /* Set the att. */
  err = do_parse (baton);
  if (err)
    return err;

  /* Close infile */
  err = svn_wc__close_adm_file (infile, path,
                                SVN_WC__ADM_ENTRIES, 0, pool);
  if (err)
    return err;
  
  if (setting)
    {
      /* Close the outfile and *sync* it, so it replaces the original
         infile. */
      err = svn_wc__close_adm_file (outfile, path,
                                    SVN_WC__ADM_ENTRIES, 1, pool);
      if (err)
        return err;
    }
  else
    {
      if (version_receiver)
        *version_receiver = baton->version;
      if (kind_receiver)
        *kind_receiver = baton->kind;
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__entry_set (svn_string_t *path,
                   svn_string_t *entryname,
                   svn_vernum_t version,
                   enum svn_node_kind kind,
                   apr_pool_t *pool,
                   ...)
{
  svn_error_t *err;
  apr_hash_t *att_hash;
  va_list ap;

  /* Convert the va_list into a hash of attributes */
  va_start (ap, pool);
  att_hash = svn_xml_ap_to_hash (ap, pool);
  va_end (ap);
  
  err = do_entry (path, pool, entryname,
                  version, NULL,
                  kind, NULL,
                  0, /* not removing */
                  1, /* setting */
                  att_hash);

  return err;
}



svn_error_t *
svn_wc__entry_get (svn_string_t *path,
                   svn_string_t *entryname,
                   svn_vernum_t *version,
                   enum svn_node_kind *kind,
                   apr_pool_t *pool,
                   apr_hash_t **hash)
{
  svn_error_t *err;
  apr_hash_t *ht = apr_make_hash (pool);

  err = do_entry (path, pool, entryname,
                  SVN_INVALID_VERNUM, version,
                  0, kind,
                  0, /* not removing */
                  0, /* not setting */
                  ht);
  if (err)
    return err;

  *hash = ht;

  return SVN_NO_ERROR;
}


/* Remove ENTRYNAME from PATH's `entries' file. */
svn_error_t *svn_wc__entry_remove (svn_string_t *path,
                                   svn_string_t *entryname,
                                   apr_pool_t *pool)
{
  svn_error_t *err;

  err = do_entry (path,
                  pool,
                  entryname,
                  SVN_INVALID_VERNUM,  /* irrelevant */
                  NULL,                /* irrelevant */
                  0,                   /* irrelevant */
                  NULL,                /* irrelevant */
                  1,                   /* removing */
                  0,                   /* not setting */
                  NULL);               /* irrelevant */
  if (err)
    return err;

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
