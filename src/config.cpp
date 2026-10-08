//routines for configuration file
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "input.h"
#include "config.h"
#include "file.h"
#include "message.h"

config::config()
{
 memset(this,0,sizeof(config));
 version=CONFIGVERSION;

 pinput[0]=ID_KEY1;
 pinput[1]=ID_NONE;
 pinput[2]=ID_NONE;
 pinput[3]=ID_NONE;

 ids.km[0]=defkeymap[0];
 ids.km[1]=defkeymap[1];
 set(CFG_FILLEDDLG,1);
}


int config::load(char *file)
{
 FILEIO f;
 if (f.open(file)) return 0;

// printf("reading %p %d %d\n",this,sizeof(config),_msize(this));
 config loaded;
 if (f.read(&loaded,sizeof(loaded)) || loaded.version!=CONFIGVERSION) {
  msg.error("Invalid or truncated configuration");return 0;
 }
 for (int i=0;i<4;++i) if (loaded.pinput[i]<ID_NONE || loaded.pinput[i]>ID_JOY2) {
  msg.error("Invalid input device in configuration");return 0;
 }
 *this=loaded;
// printf("readed %p\n",this);
 f.close();
 return 1;
}


void config::save(char *file)
{
 FILEIO f;
 if (f.create(file))
  {
   msg.error("unable to save config %s",file);
   return;
  }
 f.write(this,sizeof(config));
 f.close();
}


