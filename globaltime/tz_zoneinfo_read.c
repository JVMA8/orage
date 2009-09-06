/*
 *
 * Copyright (c) 2008 Juha Kautto  (juha at xfce.org)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the 
        Free Software Foundation
        51 Franklin Street, 5th Floor
        Boston, MA 02110-1301 USA
 */

#include <error.h>
#include <errno.h>
    /* errno */

#include <stdlib.h>
    /* malloc, atoi, free, setenv */

#include <stdio.h>
    /* printf, fopen, fread, fclose, perror, rename */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
    /* stat, mkdir */

#include <time.h>
    /* localtime, gmtime, asctime */

#include <string.h>
    /* strncmp, strcmp, strlen, strncat, strncpy, strdup, strstr */

#include "tz_zoneinfo_read.h"

/* This define is needed to get nftw instead if ftw.
 * Documentation says the define is _XOPEN_SOURCE, but it
 * does not work. __USE_XOPEN_EXTENDED works 
 * Same with _GNU_SOURCE and __USE_GNU */
#define _XOPEN_SOURCE 500
#define __USE_XOPEN_EXTENDED 1
#define _GNU_SOURCE 1
#define __USE_GNU 1
#include <ftw.h>
    /* nftw */

#define DEFAULT_OS_ZONEINFO_DIRECTORY  "/usr/share/zoneinfo"
#define ZONETAB_FILE        "zone.tab"
#define COUNTRY_FILE        "iso3166.tab"


/** This is the toplevel directory where the timezone data is installed in. */
#define ORAGE_ZONEINFO_DIRECTORY  PACKAGE_DATA_DIR "/orage/zoneinfo"

/** This is the filename of the file containing tz_convert parameters
 * This file contains the location of the os zoneinfo data.
 * the same than the above DEFAULT_OS_ZONEINFO_DIRECTORY */
#define TZ_CONVERT_PAR_FILENAME  "tz_convert.par"
#define TZ_CONVERT_PAR_FILE_LOC  ORAGE_ZONEINFO_DIRECTORY "/" TZ_CONVERT_PAR_FILENAME



/* this contains all timezone data */
orage_timezone_array tz_array={0, NULL, NULL, NULL, NULL, NULL, NULL};

char *zone_tab_buf = NULL, *country_buf = NULL;

int debug = 0; /* bigger number => more output */
char version[] = "1.4.4";
int file_cnt = 0; /* number of processed files */

unsigned char *in_buf, *in_head, *in_tail;
int in_file_base_offset = 0;

int details;

char *in_file = NULL, *out_file = NULL;
int in_file_is_dir = 0;
int excl_dir_cnt = -1;
char **excl_dir = NULL;

/* in_timezone_name is the real timezone name from the infile 
 * we are processing.
 * in_timezone_name is the timezone we are writing. Usually it is the same
 * than in_timezone_name. 
 * timezone name is for example Europe/Helsinki */
char *timezone_name = NULL;  
char *in_timezone_name = NULL;

int ignore_older = 1970; /* Ignore rules which are older or equal than this */

/* time change table starts here */
unsigned char *begin_timechanges;           

/* time change type index table starts here */
unsigned char *begin_timechangetypeindexes; 

/* time change type table starts here */
unsigned char *begin_timechangetypes;       

/* timezone name table */
unsigned char *begin_timezonenames;         

unsigned long gmtcnt;
unsigned long stdcnt;
unsigned long leapcnt;
unsigned long timecnt;  /* points when time changes */
unsigned long typecnt;  /* table of different time changes = types */
unsigned long charcnt;  /* length of timezone name table */

static void read_file(const char *file_name, const struct stat *file_stat)
{
    FILE *file;

    if (debug > 1) {
        printf("read_file: start\n");
        printf("\n***** size of file %s is %d bytes *****\n\n", file_name
                , file_stat->st_size);
    }
    in_buf = malloc(file_stat->st_size);
    in_head = in_buf;
    in_tail = in_buf + file_stat->st_size - 1;
    file = fopen(file_name, "r");
    fread(in_buf, 1, file_stat->st_size, file);
    fclose(file);
    if (debug > 1)
        printf("read_file: end\n");
}

long get_long()
{
    unsigned long tmp;

    tmp = (((long)in_head[0]<<24)
         + ((long)in_head[1]<<16)
         + ((long)in_head[2]<<8)
         +  (long)in_head[3]);
    in_head += 4;
    return(tmp);
}

int process_header()
{
    if (debug > 2)
        printf("file id: %s\n", in_head);
    if (strncmp((char *)in_head, "TZif", 4)) { /* we accept version 1 and 2 */
        return(1);
    }
    /* header */
    in_head += 4; /* type */
    in_head += 16; /* reserved */
    gmtcnt  = get_long();
    if (debug > 2)
        printf("gmtcnt=%u \n", gmtcnt);
    stdcnt  = get_long();
    if (debug > 2)
        printf("stdcnt=%u \n", stdcnt);
    leapcnt = get_long();
    if (debug > 2)
        printf("leapcnt=%u \n", leapcnt);
    timecnt = get_long();
    if (debug > 2)
        printf("number of time changes: timecnt=%u \n", timecnt);
    typecnt = get_long();
    if (debug > 2)
        printf("number of time change types: typecnt=%u \n", typecnt);
    charcnt = get_long();
    if (debug > 2)
        printf("lenght of different timezone names table: charcnt=%u \n"
                , charcnt);
    return(0);
}

process_local_time_table()
{ /* points when time changes */
    unsigned long tmp;
    int i;

    begin_timechanges = in_head;
    if (debug > 3)
        printf("\n***** printing time change dates *****\n");
    for (i = 0; i < timecnt; i++) {
        tmp = get_long();
        if (debug > 3) {
            printf("GMT %d: %u =  %s", i, tmp
                    , asctime(gmtime((const time_t*)&tmp)));
            printf("\tLOC %d: %u =  %s", i, tmp
                    , asctime(localtime((const time_t*)&tmp)));
        }
    }
}

process_local_time_type_table()
{ /* pointers to table, which explain how time changes */
    unsigned char tmp;
    int i;

    begin_timechangetypeindexes = in_head;
    if (debug > 3)
        printf("\n***** printing time change type indekses *****\n");
    for (i = 0; i < timecnt; i++) { /* we need to walk over the table */
        tmp = in_head[0];
        in_head++;
        if (debug > 3)
            printf("type %d: %d\n", i, (unsigned int)tmp);
    }
}

process_ttinfo_table()
{ /* table of different time changes = types */
    long tmp;
    unsigned char tmp2, tmp3;
    int i;

    begin_timechangetypes = in_head;
    if (debug > 3)
        printf("\n***** printing different time change types *****\n");
    for (i = 0; i < typecnt; i++) { /* we need to walk over the table */
        tmp = get_long();
        tmp2 = in_head[0];
        in_head++;
        tmp3 = in_head[0];
        in_head++;
        if (debug > 3)
            printf("%d: gmtoffset:%d isdst:%d abbr:%d\n", i, tmp
                    , (unsigned int)tmp2, (unsigned int)tmp3);
    }
}

process_abbr_table()
{
    unsigned char *tmp;
    int i;

    begin_timezonenames = in_head;
    if (debug > 3)
        printf("\n***** printing different timezone names *****\n");
    tmp = in_head;
    for (i = 0; i < charcnt; i++) { /* we need to walk over the table */
        if (debug > 3)
            printf("Abbr:%d (%d)(%s)\n", i, strlen((char *)(tmp + i)), tmp + i);
        i += strlen((char *)(tmp + i));
    }
    in_head += charcnt;
}

process_leap_table()
{
    unsigned long tmp, tmp2;
    int i;

    if (debug > 3)
        printf("\n***** printing leap time table *****\n");
    for (i = 0; i < leapcnt; i++) { /* we need to walk over the table */
        tmp = get_long();
        tmp2 = get_long();
        if (debug > 3)
            printf("leaps %d: %u =  %s (%u)", i, tmp
                    , asctime(localtime((const time_t *)&tmp)), tmp2);
    }
}

process_std_table()
{
    unsigned char tmp;
    int i;

    if (debug > 3)
        printf("\n***** printing std table *****\n");
    for (i = 0; i < stdcnt; i++) { /* we need to walk over the table */
        tmp = (unsigned long)in_head[0];
        in_head++;
        if (debug > 3)
            printf("stds %d: %d\n", i, (unsigned int)tmp);
    }
}

process_gmt_table()
{
    unsigned char tmp;
    int i;

    if (debug > 3)
        printf("\n***** printing gmt table *****\n");
    for (i = 0; i < gmtcnt; i++) { /* we need to walk over the table */
        tmp = (unsigned long)in_head[0];
        in_head++;
        if (debug > 3)
            printf("gmts %d: %d\n", i, (unsigned int)tmp);
    }
}

/* go through the contents of the file and find the positions of 
 * needed data. Uses global pointer: in_head */
int process_file(const char *file_name)
{
    if (debug > 1)
        printf("\n\nprocess_file: start\n");
    if (process_header(file_name)) {
        if (debug > 0)
            printf("File (%s) does not look like tz file. Skipping it.\n"
                    , file_name);
        return(1);
    }
    process_local_time_table();
    process_local_time_type_table();
    process_ttinfo_table();
    process_abbr_table();
    process_leap_table();
    process_std_table();
    process_gmt_table();
    if (debug > 1)
        printf("\nprocess_file: end\n\n\n");
    return(0); /* ok */
}

void get_country()
{ /* tz_array.city[tz_array.count] contains the city name.
     We will find corresponding country and fill it to the table */
    char *str, *str_nl;

    if (!(str = strstr(zone_tab_buf, tz_array.city[tz_array.count])))
        return; /* not found */
    /* we will find corresponding country code (2 char) 
     * by going to the beginning of that line. */
    for (str_nl = str; str_nl > zone_tab_buf && str_nl[0] != '\n'; str_nl--)
        ;
    /* now at the end of the previous line. 
     * There are some comments in that file, but let's play it safe and check */
    if (str_nl < zone_tab_buf)
        return; /* not found */
    /* now step one step forward and we are pointing to the country code */
    tz_array.cc[tz_array.count] = malloc(2 + 1);
    strncpy(tz_array.cc[tz_array.count], ++str_nl, 2);
    tz_array.cc[tz_array.count][2] = '\0';

    /* then search the country */
    if (!(str = strstr(country_buf, tz_array.cc[tz_array.count])))
        return; /* not found */
    /* country name is after the country code and a single tab */
    str += 3;
    /* but w still need to find how long it is.
     * It ends in the line end. 
     * (There is a line end at the end of the file also.) */
    for (str_nl = str; str_nl[0] != '\n'; str_nl++)
        ;
    tz_array.country[tz_array.count] = malloc((str_nl - str) + 1);
    strncpy(tz_array.country[tz_array.count], str, (str_nl - str));
    tz_array.country[tz_array.count][(str_nl - str)] = '\0';
}

/* FIXME: need to check that if OUTFILE is given as a parameter,
 * INFILE is not a directory (or make outfile to act like directory also ? */
int write_ical_file(const char *in_file_name, const struct stat *in_file_stat)
{
    int i;
    unsigned int tct_i, abbr_i;
    struct tm cur_gm_time;
    time_t tt_now = time(NULL);
    long tc_time = 0; /* TimeChange time */
    char s_next[101];

    if (debug > 1)
        printf("***** write_ical_file: start *****\n\n");

    tz_array.city[tz_array.count] = strdup(in_timezone_name);

    tz_array.cc[tz_array.count] = NULL;
    tz_array.country[tz_array.count] = NULL;
    if (details)
        get_country();

    in_head = begin_timechanges;
    for (i = 0; (i < timecnt) && (tc_time <= tt_now); i++) {
        /* search for current time setting.
         * timecnt tells how many changes we have in the tz file.
         * i points to the next value to read. */
        tc_time = get_long(); /* start time of this timechange */
    }
    /* i points to the next value to be read, so need to -- */
    if (--i < 0 && typecnt == 0) { 
        /* we failed to find any timechanges that have happened earlier than
         * now and there are no changes defined, so use default UTC=GMT */
        tz_array.utc_offset[tz_array.count] = 0;
        tz_array.dst[tz_array.count] = 0;
        tz_array.tz[tz_array.count] = "UTC";
        tz_array.next[tz_array.count] = NULL;
        tz_array.count++;
        return(1); /* done */
    }
    if (tc_time > tt_now) {
        /* we found previous and next value */
        /* tc_time has the next change time */
        if (details) {
            localtime_r((const time_t *)&tc_time, &cur_gm_time);
            strftime(s_next, 100, "%c", &cur_gm_time);
            tz_array.next[tz_array.count] = strdup(s_next);
        }
        else 
            tz_array.next[tz_array.count] = NULL;
        i--; /* we need to take the previous value */
    }
    else 
        tz_array.next[tz_array.count] = NULL;

    /* i now points to latest time change and shows current time.
     * So we found our result and can start collecting real data: */

    /* get timechange type index */
    if (timecnt) {
        in_head = begin_timechangetypeindexes;
        tct_i = (unsigned int)in_head[i];
    }
    else 
        tct_i = 0;

    /* get timechange type */
    in_head = begin_timechangetypes;
    in_head += 6*tct_i;
    tz_array.utc_offset[tz_array.count] = (int)get_long();
    tz_array.dst[tz_array.count] = in_head[0];
    abbr_i =  in_head[1];

     /* get timezone name */
    in_head = begin_timezonenames;
    tz_array.tz[tz_array.count] = strdup((char *)in_head + abbr_i);

    tz_array.count++;
    if (debug > 1)
        printf("\n***** write_ical_file: end *****\n\n\n");
    return(0);
}

/* The main code. This is called once per each file found */
int file_call(const char *file_name, const struct stat *sb, int flags
        , struct FTW *f)
{
    int i;

    if (debug > 1)
        printf("file_call: start\n");
    file_cnt++;
    /* we are only interested about files and directories we can access */
    if (flags == FTW_F) { /* we got file */
        if (debug > 0)
            printf("\t\tfile_call: processing file=(%s)\n", file_name);
        read_file(file_name, sb);
        if (process_file(file_name)) { /* we skipped this file */
            free(in_buf);
            return(FTW_CONTINUE);
        }
        in_timezone_name = strdup(&file_name[in_file_base_offset 
                + strlen("zoneinfo/")]);
        timezone_name = strdup(in_timezone_name);
        write_ical_file(file_name, sb);

        free(in_buf);
        free(out_file);
        out_file = NULL;
        free(in_timezone_name);
        free(timezone_name);
    }
    else if (flags == FTW_D) { /* this is directory */
        if (debug > 0)
            printf("\tfile_call: processing directory=(%s)\n", file_name);
        /* need to check if we have excluded directory */
        for (i = 0; (i <= excl_dir_cnt) && excl_dir[i]; i++) {
            if (strcmp(excl_dir[i],  file_name+f->base) == 0) {
                if (debug > 0)
                    printf("\t\tfile_call: skipping excluded directory (%s)\n"
                            , file_name+f->base);
                return(FTW_SKIP_SUBTREE);
            }
        }
    }
    else if (flags == FTW_SL) {
        if (debug > 0) {
            printf("\t\tfile_call: skipping symbolic link=(%s)\n", file_name);
        }
    }
    else {
        if (debug > 0) {
            printf("\t\tfile_call: skipping inaccessible file=(%s)\n", file_name);
        }
    }

    if (debug > 1)
        printf("file_call: end\n");
    return(FTW_CONTINUE);
}

/* check the parameters and use defaults when possible */
int check_parameters()
{
    char *s_tz, *last_tz = NULL, tz[]="/zoneinfo", tz2[]="zoneinfo/";
    int tz_len, i;
    struct stat in_stat;
    FILE *par_file;
    struct stat par_file_stat;

    if (debug > 1)
        printf("check_parameters: start\n");

    par_file = fopen(TZ_CONVERT_PAR_FILE_LOC, "r");
    if (par_file != NULL) { /* does exist and no error */
        if (stat(TZ_CONVERT_PAR_FILE_LOC, &par_file_stat) == -1) {
            /* error reading the parameter file */
            printf("check_parameters: in_file name not found from (%s) \n"
                , TZ_CONVERT_PAR_FILE_LOC);
            fclose(par_file);
        }
        else { /* no errors */
            in_file = malloc(par_file_stat.st_size+1);
            fread(in_file, 1, par_file_stat.st_size, par_file);
            if (ferror(par_file)) {
                printf("check_parameters: error reading (%s)\n"
                        , TZ_CONVERT_PAR_FILE_LOC);
                free(in_file);
                fclose(par_file);
            }
            else { 
                /* terminate with nul */
                if (in_file[par_file_stat.st_size-1] == '\n')
                    in_file[par_file_stat.st_size-1] = '\0';
                else
                    in_file[par_file_stat.st_size] = '\0';
                /* test that it is fine */
                if (stat(in_file, &par_file_stat) == -1) { /* error */
                    printf("check_parameters: error reading (%s) (from %s)\n"
                            , in_file, TZ_CONVERT_PAR_FILE_LOC);
                    free(in_file);
                    in_file = NULL;
                }
            }
        }
    }
    if (in_file == NULL) /* in file not found */
        in_file = strdup(DEFAULT_OS_ZONEINFO_DIRECTORY);

    if (in_file[0] != '/') {
        printf("check_parameters: in_file name (%s) is not absolute file name. Ending\n"
                , in_file);
        return(1);
    }
    if (stat(in_file, &in_stat) == -1) { /* error */
        perror("\tcheck_parameters: stat");
        return(2);
    }
    if (S_ISDIR(in_stat.st_mode)) {
        in_file_is_dir = 1;
        if (timezone_name) {
            printf("\tcheck_parameters: when infile (%s) is directory, you can not specify timezone name (%s), but it is copied from each in file. Ending\n"
                , in_file, timezone_name);
            return(3);
        }
        if (out_file) {
            printf("\tcheck_parameters: when infile (%s) is directory, you can not specify outfile name (%s), but it is copied from each in file. Ending\n"
                , in_file, out_file);
            return(3);
        }
    }
    else {
        in_file_is_dir = 0;
        if (!S_ISREG(in_stat.st_mode)) {
            printf("\tcheck_parameters: in_file (%s) is not directory nor normal file. Ending\n"
                , in_file);
            return(3);
        }
    }

    /* find last "/zoneinfo" from the infile (directory) name. 
     * Normally there is only one. 
     * It needs to be at the end of the string or be followed by '/' */
    tz_len = strlen(tz);
    s_tz = in_file;
    for (s_tz = strstr(s_tz, tz); s_tz != NULL; s_tz = strstr(s_tz, tz)) {
        if (s_tz[tz_len] == '\0' || s_tz[tz_len] == '/')
            last_tz = s_tz;
        *s_tz++;
    }
    if (last_tz == NULL) {
        printf("check_parameters: in_file name (%s) does not contain (%s). Ending\n"
                , in_file, tz);
        return(4);
    }

    in_file_base_offset = last_tz - in_file + 1; /* skip '/' */

    if (!in_file_is_dir) {
        in_timezone_name = strdup(&in_file[in_file_base_offset + strlen(tz2)]);
        if (timezone_name == NULL)
            timezone_name = strdup(in_timezone_name);
    }

    if (excl_dir == NULL) { /* use default */
        excl_dir_cnt = 5; /* just in case it was changed by parameter */
        excl_dir = calloc(3, sizeof(char *));
        excl_dir[0] = strdup("posix");
        excl_dir[1] = strdup("right");
    }

    if (debug > 1) {
        printf("\n***** Parameters *****\n");
        printf("\tversion: %s\n", version);
        printf("\tdebug level: %d\n", debug);
        printf("\tyear limit: %d\n", ignore_older);
        printf("\tinfile: (%s) %s\n", in_file
                , in_file_is_dir ? "directory" : "normal file");
        printf("\tinfile timezone: (%s)\n", in_timezone_name);
        printf("\toutfile: (%s)\n", out_file);
        printf("\toutfile timezone: (%s)\n", timezone_name);
        printf("\tmaximum exclude directory count: (%d)\n", excl_dir_cnt);
        for (i = 0; (i <= excl_dir_cnt) && excl_dir[i];i++)
            printf("\t\texclude directory %d: (%s)\n"
                    , i, excl_dir[i]);
        printf("***** Parameters *****\n\n");
    }

    if (debug > 1)
        printf("check_parameters: end\n");
    return(0); /* continue */
}

void read_countries()
{
    char *tz_dir, *zone_tab_file_name, *country_file_name;
    int zoneinfo_len=strlen("zoneinfo/");
    FILE *zone_tab_file, *country_file;
    struct stat zone_tab_file_stat, country_file_stat;

    tz_dir = malloc(in_file_base_offset + zoneinfo_len + 1); /* '\0' */
    strncpy(tz_dir, in_file, in_file_base_offset);
    tz_dir[in_file_base_offset] = '\0'; 
    strcat(tz_dir, "zoneinfo/"); /* now we have the base directory */

    zone_tab_file_name = malloc(strlen(tz_dir) + strlen(ZONETAB_FILE) + 1);
    strcpy(zone_tab_file_name, tz_dir);
    strcat(zone_tab_file_name, ZONETAB_FILE);

    country_file_name = malloc(strlen(tz_dir) + strlen(COUNTRY_FILE) + 1);
    strcpy(country_file_name, tz_dir);
    strcat(country_file_name, COUNTRY_FILE);

    /*
    printf("read_countries: tzdir:(%s) zone.tab:(%s) iso.tab:(%s)\n"
            , tz_dir, zone_tab_file_name, country_file_name);
    */

    free(tz_dir);

    /****** zone.tab file ******/
    if (zone_tab_buf) {
        free(zone_tab_file_name);
        free(country_file_name);
        return;
    }
    if (!(zone_tab_file = fopen(zone_tab_file_name, "r"))) {
        printf("read_countries: zone.tab file open failed (%s)\n"
                , zone_tab_file_name);
        free(zone_tab_file_name);
        free(country_file_name);
        perror("\tfopen");
        return;
    }
    if (stat(zone_tab_file_name, &zone_tab_file_stat) == -1) {
        printf("read_countries: zone.tab file stat failed (%s)\n"
                , zone_tab_file_name);
        free(zone_tab_file_name);
        free(country_file_name);
        fclose(zone_tab_file);
        perror("\tstat");
        return;
    }
    zone_tab_buf = malloc(zone_tab_file_stat.st_size+1);
    fread(zone_tab_buf, 1, zone_tab_file_stat.st_size, zone_tab_file);
    if (ferror(zone_tab_file)) {
        printf("read_countries: zone.tab file read failed (%s)\n"
                , zone_tab_file_name);
        free(zone_tab_file_name);
        free(country_file_name);
        fclose(zone_tab_file);
        perror("\tfread");
        return;
    }
    zone_tab_buf[zone_tab_file_stat.st_size] = '\0';
    free(zone_tab_file_name);
    fclose(zone_tab_file);

    /****** country=iso3166.tab file ******/
    if (country_buf) {
        free(country_file_name);
        return;
    }
    if (!(country_file = fopen(country_file_name, "r"))) {
        printf("read_countries: iso3166.tab file open failed (%s)\n"
                , country_file_name);
        free(country_file_name);
        perror("\tfopen");
        return;
    }
    if (stat(country_file_name, &country_file_stat) == -1) {
        printf("read_countries: iso3166.tab file stat failed (%s)\n"
                , country_file_name);
        free(country_file_name);
        fclose(country_file);
        perror("\tstat");
        return;
    }
    country_buf = malloc(country_file_stat.st_size+1);
    fread(country_buf, 1, country_file_stat.st_size, country_file);
    if (ferror(country_file)) {
        printf("read_countries: iso3166.tab file read failed (%s)\n"
                , country_file_name);
        free(country_file_name);
        fclose(country_file);
        perror("\tfread");
        return;
    }
    country_buf[country_file_stat.st_size] = '\0';
    free(country_file_name);
    fclose(country_file);
}

orage_timezone_array get_orage_timezones(int show_details)
{
    int tz_array_size = 1000; /* FIXME: this needs to be counted */
    /*
     icalarray *tz_array;
     icaltimezone *l_tz;
     struct icaltimetype ctime;
   */

    details = show_details;
    if (tz_array.count == 0) {
        tz_array.city = (char **)malloc(sizeof(char *)*(tz_array_size+2));
        tz_array.utc_offset = (int *)malloc(sizeof(int)*(tz_array_size+2));
        tz_array.dst = (int *)malloc(sizeof(int)*(tz_array_size+2));
        tz_array.tz = (char **)malloc(sizeof(char *)*(tz_array_size+2));
        tz_array.next = (char **)malloc(sizeof(char *)*(tz_array_size+2));
        tz_array.country = (char **)malloc(sizeof(char *)*(tz_array_size+2));
        tz_array.cc = (char **)malloc(sizeof(char *)*(tz_array_size+2));
    /* nftw goes through the whole file structure and calls "file_call"
     * with each file. It returns 0 when everything has been done and -1
     * if it run into an error. */
        check_parameters();
        if (debug > 0)
            printf("Processing %s files\n", in_file);
        if (details)
            read_countries();
        if (nftw(in_file, file_call, 10, FTW_PHYS | FTW_ACTIONRETVAL) == -1) {
            perror("nftw error in file handling");
            exit(EXIT_FAILURE);
        }
        printf("Orage: Processed %d timezone files from (%s)\n"
                , file_cnt, in_file);

        free(in_file);

        tz_array.utc_offset[tz_array.count] = 0;
        tz_array.dst[tz_array.count] = 0;
        tz_array.tz[tz_array.count] = strdup("UTC");
        tz_array.next[tz_array.count] = NULL;
        tz_array.country[tz_array.count] = NULL;
        tz_array.cc[tz_array.count] = NULL;
        tz_array.city[tz_array.count++] = strdup("UTC");

        tz_array.utc_offset[tz_array.count] = 0;
        tz_array.dst[tz_array.count] = 0;
        tz_array.tz[tz_array.count] = NULL;
        tz_array.next[tz_array.count] = NULL;
        tz_array.country[tz_array.count] = NULL;
        tz_array.cc[tz_array.count] = NULL;
        tz_array.city[tz_array.count++] = strdup("floating");
    }
    return (tz_array);
}

void free_orage_timezones(int show_details)
{
    int i;

    for (i = 0 ; i < tz_array.count; i++) {
        if (tz_array.city[i])
            free(tz_array.city[i]);
        if (tz_array.tz[i])
            free(tz_array.tz[i]);
        if (tz_array.next[i])
            free(tz_array.next[i]);
        if (tz_array.country[i])
            free(tz_array.country[i]);
        if (tz_array.cc[i])
            free(tz_array.cc[i]);
    }
    free(tz_array.city);
    free(tz_array.utc_offset);
    free(tz_array.dst);
    free(tz_array.tz);
    free(tz_array.next);
    free(tz_array.country);
    free(tz_array.cc);
    tz_array.count = 0;
    timezone_name = NULL;
    file_cnt = 0; /* number of processed files */
}