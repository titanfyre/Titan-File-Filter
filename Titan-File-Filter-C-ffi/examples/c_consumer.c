#include "TitanFileFilterJsonSDK.h"
#include <string.h>
int main(void){uint64_t session=0;if(titan_file_filter_json_open(&session))return 1;char *reply=titan_file_filter_json_call(session,"{",1);if(!reply){titan_file_filter_json_close(session);return 2;}int failed=strstr(reply,"\"ok\":false")==0;titan_file_filter_json_free(reply);if(titan_file_filter_json_close(session))return 3;return failed;}
