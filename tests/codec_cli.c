#include "dc_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int nibble(char c) {if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
int main(int argc,char **argv) {
 char line[256];uint8_t data[128],out[DC_UART_MAX];size_t n,i,k;int a,b;dc_frame_t f;dc_parser_t parser;
 if(argc!=2||(strcmp(argv[1],"encode")&&strcmp(argv[1],"decode")))return 2;
 while(fgets(line,sizeof(line),stdin)) {
  n=strcspn(line,"\r\n");if(n%2||n/2>sizeof(data)) {puts("ERROR");continue;}
  for(i=0;i<n/2;i++) {a=nibble(line[2*i]);b=nibble(line[2*i+1]);if(a<0||b<0)break;data[i]=(uint8_t)(a*16+b);}
  if(i!=n/2) {puts("ERROR");continue;}
  n/=2;k=0;
  if(!strcmp(argv[1],"encode")) {if(dc_decode_raw(data,n,&f)==DC_OK)k=dc_encode_uart(&f,out,sizeof(out));}
  else {unsigned count=0;dc_parser_init(&parser);for(i=0;i<n;i++)if(dc_parser_feed(&parser,data[i],&f))++count;
   if(count==1&&n&&data[n-1]==0&&!parser.invalid&&!parser.oversize)k=dc_encode_raw(&f,out,sizeof(out));}
  if(!k) {puts("ERROR");continue;}
  for(i=0;i<k;i++)printf("%02x",out[i]);
  putchar('\n');
 }
 return 0;
}
