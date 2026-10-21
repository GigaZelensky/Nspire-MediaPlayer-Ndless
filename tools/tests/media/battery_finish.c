#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#define MAX_PATH_LEN 512
static bool samples,stopped,prepare_ok,path_ok,open_ok,report_ok,close_ok;
static unsigned opens,closes,messages;
static bool battery_calibration_has_samples(void){return samples;}
static bool battery_calibration_stopped(void){return stopped;}
static bool player_data_prepare(void){return prepare_ok;}
static bool player_data_path(const char *name,char *out,size_t n)
{
    assert(!strcmp(name,NDMEDIA_BATTERY_LOAD_TEST?"nMedia-battery-load.csv.tns":"nMedia-battery-calibration.csv.tns"));
    snprintf(out,n,"test-report");return path_ok;
}
static FILE *test_open(const char *path,const char *mode)
{assert(!strcmp(path,"test-report") && !strcmp(mode,"wb"));++opens;return open_ok?tmpfile():NULL;}
static bool battery_calibration_report(FILE *f){fputs("saved calibration\n",f);return report_ok;}
static int test_close(FILE *f){++closes;int result=fclose(f);return close_ok?result:EOF;}
static void queue_player_message(const char *title,const char *text)
{assert(!strcmp(title,"Battery calibration") && strstr(text,"could not be saved"));++messages;}
#define fopen test_open
#define fclose test_close
/* PRODUCTION_FINISH */
#undef fopen
#undef fclose
static void reset(void)
{samples=stopped=true;prepare_ok=path_ok=open_ok=report_ok=close_ok=true;opens=closes=messages=0;}
int main(void)
{
    reset();assert(save_battery_calibration_report() && opens==1 && closes==1 && !messages);
    reset();samples=stopped=false;assert(!save_battery_calibration_report() && !opens && !messages);
    reset();stopped=false;assert(!save_battery_calibration_report() && closes==1 && !messages);
    reset();prepare_ok=false;assert(!save_battery_calibration_report() && !opens && messages==1);
    reset();path_ok=false;assert(!save_battery_calibration_report() && !opens && messages==1);
    reset();open_ok=false;assert(!save_battery_calibration_report() && opens==1 && !closes && messages==1);
    reset();report_ok=false;assert(!save_battery_calibration_report() && closes==1 && messages==1);
    reset();close_ok=false;assert(!save_battery_calibration_report() && closes==1 && messages==1);
    puts("PASS unattended calibration finish: closed verified report permits standby without an OK dialog; all write failures remain visible");
    return 0;
}
