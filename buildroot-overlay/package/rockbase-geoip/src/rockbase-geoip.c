/*
 * rockbase-geoip - offline GeoLite2-City IP geolocation lookup.
 *
 * Reads IPv4/IPv6 addresses from stdin (one per line, or comma/space
 * separated) and prints one JSON object per line:
 *   {"ip":"1.2.3.4","lat":34.77,"lon":113.72,"city":"Zhengzhou","country":"China"}
 *   {"ip":"bad","error":"not found"}
 *
 * The mmdb is opened once and mmap'd; lookups are O(log n) tree walks.
 *
 * Usage: rockbase-geoip [mmdb-path]
 * Default mmdb search order: /root/config/ then /usr/share/ (see main).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <maxminddb.h>

#define MAX_IPS_PER_LINE 64

static void json_escape(const char *s, char *out, size_t outsz)
{
    size_t o = 0;
    for (const char *p = s; *p && o + 6 < outsz; p++) {
        switch (*p) {
        case '"':  out[o++] = '\\'; out[o++] = '"'; break;
        case '\\': out[o++] = '\\'; out[o++] = '\\'; break;
        case '\n': out[o++] = '\\'; out[o++] = 'n'; break;
        case '\r': out[o++] = '\\'; out[o++] = 'r'; break;
        case '\t': out[o++] = '\\'; out[o++] = 't'; break;
        default:
            if ((unsigned char)*p < 0x20) {
                o += (size_t)snprintf(out + o, outsz - o, "\\u%04x", *p);
            } else {
                out[o++] = *p;
            }
        }
    }
    out[o] = '\0';
}

/* libmaxminddb >= 1.10 API: lookup returns the result struct by value and
 * MMDB_get_value takes an MMDB_entry_data_s. */
static int lookup_one(MMDB_s *mmdb, const char *ip, FILE *out)
{
    int gai_error = 0, mmdb_error = 0;
    MMDB_lookup_result_s res =
        MMDB_lookup_string(mmdb, ip, &gai_error, &mmdb_error);
    if (!res.found_entry) {
        char esc[128];
        json_escape(ip, esc, sizeof(esc));
        fprintf(out, "{\"ip\":\"%s\",\"error\":\"not found\"}\n", esc);
        return 0;
    }

    MMDB_entry_data_s data;
    double lat = 0.0, lon = 0.0;
    int has_loc = 0;
    char city[128] = "", country[128] = "";

    /* location.latitude / location.longitude */
    if (MMDB_get_value(&res.entry, &data, "location", "latitude", NULL) == MMDB_SUCCESS &&
        data.has_data && data.type == MMDB_DATA_TYPE_DOUBLE) {
        lat = data.double_value;
        has_loc = 1;
    }
    if (MMDB_get_value(&res.entry, &data, "location", "longitude", NULL) == MMDB_SUCCESS &&
        data.has_data && data.type == MMDB_DATA_TYPE_DOUBLE) {
        lon = data.double_value;
        has_loc = 1;
    }
    /* city.names.en (fallback: city.name) */
    if (MMDB_get_value(&res.entry, &data, "city", "names", "en", NULL) == MMDB_SUCCESS &&
        data.has_data && data.type == MMDB_DATA_TYPE_UTF8_STRING) {
        snprintf(city, sizeof(city), "%.*s", (int)data.data_size, data.utf8_string);
    } else if (MMDB_get_value(&res.entry, &data, "city", "name", NULL) == MMDB_SUCCESS &&
               data.has_data && data.type == MMDB_DATA_TYPE_UTF8_STRING) {
        snprintf(city, sizeof(city), "%.*s", (int)data.data_size, data.utf8_string);
    }
    /* country.names.en (fallback: country.name) */
    if (MMDB_get_value(&res.entry, &data, "country", "names", "en", NULL) == MMDB_SUCCESS &&
        data.has_data && data.type == MMDB_DATA_TYPE_UTF8_STRING) {
        snprintf(country, sizeof(country), "%.*s", (int)data.data_size, data.utf8_string);
    } else if (MMDB_get_value(&res.entry, &data, "country", "name", NULL) == MMDB_SUCCESS &&
               data.has_data && data.type == MMDB_DATA_TYPE_UTF8_STRING) {
        snprintf(country, sizeof(country), "%.*s", (int)data.data_size, data.utf8_string);
    }

    char ip_esc[128], city_esc[128], country_esc[128];
    json_escape(ip, ip_esc, sizeof(ip_esc));
    json_escape(city, city_esc, sizeof(city_esc));
    json_escape(country, country_esc, sizeof(country_esc));

    if (has_loc) {
        fprintf(out,
                "{\"ip\":\"%s\",\"lat\":%.4f,\"lon\":%.4f,"
                "\"city\":\"%s\",\"country\":\"%s\"}\n",
                ip_esc, lat, lon, city_esc, country_esc);
    } else {
        fprintf(out,
                "{\"ip\":\"%s\",\"lat\":null,\"lon\":null,"
                "\"city\":\"%s\",\"country\":\"%s\"}\n",
                ip_esc, city_esc, country_esc);
    }
    return 0;
}

/* Default mmdb search order:
 * 1. /root/config/GeoLite2-City.mmdb  - config partition, hot-fix escape
 *    hatch only (scp override for emergency validation; not OTA-managed).
 * 2. /usr/share/GeoLite2-City.mmdb    - rootfs, the regular source of truth,
 *    updated with every OTA release.
 */
static const char *default_mmdb_paths[] = {
    "/root/config/GeoLite2-City.mmdb",
    "/usr/share/GeoLite2-City.mmdb",
};

int main(int argc, char **argv)
{
    const char *mmdb_path = NULL;
    if (argc > 1)
        mmdb_path = argv[1];
    else {
        for (size_t i = 0; i < sizeof(default_mmdb_paths) / sizeof(default_mmdb_paths[0]); i++) {
            struct stat st;
            if (stat(default_mmdb_paths[i], &st) == 0 && S_ISREG(st.st_mode)) {
                mmdb_path = default_mmdb_paths[i];
                break;
            }
        }
        if (!mmdb_path)
            mmdb_path = default_mmdb_paths[0]; /* report the primary path in errors */
    }

    MMDB_s mmdb;
    int rc = MMDB_open(mmdb_path, MMDB_MODE_MMAP, &mmdb);
    if (rc != MMDB_SUCCESS) {
        fprintf(stderr, "rockbase-geoip: cannot open %s (error %d)\n",
                mmdb_path, rc);
        return 1;
    }

    char line[4096];
    while (fgets(line, sizeof(line), stdin)) {
        /* strip trailing newline */
        line[strcspn(line, "\r\n")] = '\0';
        char *save = line;
        while (*save) {
            /* skip separators */
            while (*save && (isspace((unsigned char)*save) || *save == ','))
                save++;
            if (!*save)
                break;
            char *end = save;
            while (*end && !isspace((unsigned char)*end) && *end != ',')
                end++;
            int len = (int)(end - save);
            if (*end)
                *end = '\0';
            if (len > 0 && len < 64)
                lookup_one(&mmdb, save, stdout);
            save = end;
        }
    }

    MMDB_close(&mmdb);
    return 0;
}
