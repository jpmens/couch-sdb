#include <config.h>
#include <stdio.h>
#include <string.h>
#include "cdbc.h"

#include <isc/print.h>
#include <isc/result.h>
#include <isc/util.h>
#include <isc/mem.h>
#include <dns/sdb.h>
#include <dns/log.h>
#include <dns/lib.h>
#include <named/globals.h>

#include <isc/file.h>
#include <isc/lib.h>
#include <isc/log.h>
#include <isc/msgs.h>
#include <isc/msgcat.h>
#include <isc/region.h>

#define DEFAULT_TTL	3600

static dns_sdbimplementation_t *couch = NULL;

struct jpinfo {
	char *uri;			// CouchDB scheme://host:port
	char *dbname;			// CouchDB database name
	CDBC *cd;
};

/*
 * "Fix" rhs of name, depending on RR type.
 * If name contains dots, add dot to end if non-existent.
 * Otherwise, leave alone because it is relative to apex.
 */
static char *fixrdata(const char *type, const char *name)
{
	static char namebuf[512];

	strncpy(namebuf, name, sizeof(namebuf) - 1);
	namebuf[sizeof(namebuf) - 1] = 0;

	if (!strcasecmp(type, "A") || !strcasecmp(type, "TXT"))
		return (namebuf);

	if (strchr(namebuf, '.') != NULL) {
		if (namebuf[strlen(namebuf) - 1] != '.')
			strcat(namebuf, ".");
	}
	return (namebuf);
}


static isc_result_t
couch_create(const char *zone, int argc, char **argv, void *driverdata, void **dbdata)
{
	struct jpinfo *jpi;

	UNUSED(zone);
	UNUSED(driverdata);

	printf("***************** couch_create\n");

	if (argc != 2) {
		return (ISC_R_FAILURE);
	}

	jpi = isc_mem_get(ns_g_mctx, sizeof(struct jpinfo));
	if (jpi == NULL)
		return (ISC_R_NOMEMORY);

	if ((jpi->uri = isc_mem_strdup(ns_g_mctx, argv[0])) == NULL) 
		return (ISC_R_NOMEMORY);
	if ((jpi->dbname = isc_mem_strdup(ns_g_mctx, argv[1])) == NULL) 
		return (ISC_R_NOMEMORY);
		
	if ((jpi->cd = cdbc_new(jpi->uri)) == NULL) {
		fprintf(stderr, "Can't init CDBC\n");
	}

	cdbc_usedb(jpi->cd, jpi->dbname);

	printf("***************** couch_create ends\n");

	*dbdata = jpi;
	return (ISC_R_SUCCESS);

	


}

static void
couch_destroy(const char *zone, void *driverdata, void **dbdata)
{
	struct jpinfo *jpi = *dbdata;

	UNUSED(zone);
	UNUSED(driverdata);

	cdbc_free(jpi->cd);

	isc_mem_free(ns_g_mctx, jpi->uri);
	isc_mem_free(ns_g_mctx, jpi->dbname);
	isc_mem_put(ns_g_mctx, jpi, sizeof(struct jpinfo));
}

static isc_result_t
to_sdb(dns_sdblookup_t *l, const char *type, int ttl, const char *name, int *count)
{
	isc_result_t res;
	char *outdata = fixrdata(type, name);
	char buf[1024];

	if (strcasecmp(type, "TXT") == 0) {
		sprintf(buf, "\"%s\"", outdata);
		res = dns_sdb_putrr(l, type, ttl, buf);
	} else {
		res = dns_sdb_putrr(l, type, ttl, outdata);
	}
	if (res != ISC_R_SUCCESS) {
		isc_log_iwrite(dns_lctx,
			DNS_LOGCATEGORY_DATABASE,
			DNS_LOGMODULE_SDB, ISC_LOG_ERROR,
			isc_msgcat, ISC_MSGSET_GENERAL,
			ISC_MSG_FAILED, "dns_sdb_putrr");
	} else {
		printf("--> %2d %s %s\n", ++*count, type, name);
	}
	return (res);

}
static isc_result_t
couch_lookup(const char *zone, const char *name, void *dbdata, dns_sdblookup_t *l)
{
	isc_result_t res;
	struct jpinfo *jpi = dbdata;
	char *mname = NULL;
	int rev = -1, serial = -1,  ttl, count = 0;
	unsigned int n;
	json_t *doc, *soa, *o, *ns, *rr;

	printf("************ couch_lookup (%s, %s)\n", zone, name);

	if ((doc = cdbc_get_js(jpi->cd, zone)) == NULL) {
		return (ISC_R_NOTFOUND);
	}

	if (strcmp(name, "@") == 0) {
	
		if ((soa = json_object_get(doc, "soa")) == NULL) {
			puts("NO SOA!!!");
		}
	
		/*
		 * Get document's revision and use as serial number if
		 * none specified in SOA
		 */
	
		o = json_object_get(doc, "_rev");
		rev = atoi(json_string_value(o));
	
		if ((o = json_object_get(soa, "mname"))) {
			mname = strdup(json_string_value(o));
		}
	
		if ((o = json_object_get(soa, "serial"))) {
			serial = json_integer_value(o);
			serial = (serial) ? serial : rev;
		} else {
			serial = rev;
		}
	
		res = dns_sdb_putsoa(l, "hello", (mname) ? mname : "nobody.here.", serial);
		if (res != ISC_R_SUCCESS) {
			isc_log_iwrite(dns_lctx,
				DNS_LOGCATEGORY_DATABASE,
				DNS_LOGMODULE_SDB, ISC_LOG_ERROR,
				isc_msgcat, ISC_MSGSET_GENERAL,
				ISC_MSG_FAILED, "dns_sdb_putsoa");
			return (ISC_R_FAILURE);
		}
		if (mname)
			free(mname);
		
	
		/* NS RR */
	
		if ((ns = json_object_get(doc, "ns")) != NULL) {
			for (n = 0; n < json_array_size(ns); n++) {
				o = json_array_get(ns, n);
				printf("NS  %s\n", json_string_value(o));
				res = to_sdb(l, "NS", 86400, json_string_value(o), &count);
				if (res != ISC_R_SUCCESS) {
					return (ISC_R_FAILURE);
				}
			}
		}

		return (ISC_R_SUCCESS);
	}

	/* Other RR */

	if ((rr = json_object_get(doc, "rr")) != NULL) {
		for (n = 0; n < json_array_size(rr); n++) {
			json_t *domain, *type, *rdata, *ttl_j;
			int j;

			o = json_array_get(rr, n);
			printf("OBJ = %s\n", json_dumps(o, 0));

			domain = json_object_get(o, "name");
			type = json_object_get(o, "type");
			ttl_j = json_object_get(o, "ttl");

			ttl = (ttl_j) ? json_integer_value(ttl_j) : DEFAULT_TTL;

			/* Ignore unwanted names */
			if (strcasecmp(name, json_string_value(domain)) != 0)
				continue;

			rdata = json_object_get(o, "data");
			if (json_is_array(rdata)) {

				/* data is an array of rdata; cycle through that
				 * building resource records
				 */

				for (j = 0; j < json_array_size(rdata); j++) {
					json_t *d = json_array_get(rdata, j);

					res = to_sdb(l, json_string_value(type), ttl, json_string_value(d), &count);
					if (res != ISC_R_SUCCESS) {
						return (ISC_R_FAILURE);
					}
				}
			} else {
				res = to_sdb(l, json_string_value(type), ttl, json_string_value(rdata), &count);
				if (res != ISC_R_SUCCESS) {
					return (ISC_R_FAILURE);
				}
			}
		}
	}

	printf("COUNT == %d\n", count);
	return (count > 0) ? ISC_R_SUCCESS : ISC_R_NOTFOUND;
}


static dns_sdbmethods_t	couch_methods = {
	couch_lookup,		// lookup
	NULL,			// authority
	NULL,			// allnodes
	couch_create,		// create
	couch_destroy		// destroy
};

isc_result_t
couch_init(void)
{
	unsigned int flags;

	flags = DNS_SDBFLAG_RELATIVEOWNER | DNS_SDBFLAG_RELATIVERDATA;

	return (dns_sdb_register("couch", &couch_methods, NULL, flags,
			ns_g_mctx, &couch));
}

void couch_clear(void)
{
	if (couch != NULL)
		dns_sdb_unregister(&couch);
}
