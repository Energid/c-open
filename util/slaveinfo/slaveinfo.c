/*********************************************************************
 *        _       _         _
 *  _ __ | |_  _ | |  __ _ | |__   ___
 * | '__|| __|(_)| | / _` || '_ \ / __|
 * | |   | |_  _ | || (_| || |_) |\__ \
 * |_|    \__|(_)|_| \__,_||_.__/ |___/
 *
 * www.rt-labs.com
 * Copyright 2017 rt-labs AB, Sweden.
 *
 * This software is dual-licensed under GPLv3 and a commercial
 * license. See the file LICENSE.md distributed with this software for
 * full license information.
 ********************************************************************/

#include "slaveinfo.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <co_api.h>
#include <co_obj.h>
#include <co_main.h>
#include "co_util.h"

#include "osal.h"

typedef struct amp_slave
{
   uint8_t node;
} amp_slave_t;

int slaveinfo (const char * canif, uint8_t node, int bitrate)
{
   co_net_t * net;
   co_cfg_t cfg = {0};
   co_client_t * client;

   struct pdo_val
   {
      uint16_t statusWord;
      int32_t actualPosition;
      int16_t actualTorque;
      uint16_t controlWord;
      int32_t targetPosition;
   } pdoVal;

   static co_entry_t nullEntry = {0, 0, 0, 0, 0, NULL};
   co_entry_t statusWord =
      {0, OD_RPDO | OD_TPDO, DTYPE_UNSIGNED16, 16, 0, &pdoVal.statusWord};
   co_entry_t actualPosition =
      {0, OD_RPDO | OD_TPDO, DTYPE_INTEGER32, 32, 0, &pdoVal.actualPosition};
   co_entry_t actualTorque =
      {0, OD_RPDO | OD_TPDO, DTYPE_INTEGER16, 16, 0, &pdoVal.actualTorque};
   co_entry_t controlWord =
      {0, OD_RPDO | OD_TPDO, DTYPE_UNSIGNED16, 16, 0, &pdoVal.controlWord};
   co_entry_t targetPosition =
      {0, OD_RPDO | OD_TPDO, DTYPE_INTEGER32, 32, 0, &pdoVal.targetPosition};

   const co_obj_t od_none[] = {
      {0x1400, OTYPE_NULL, 0, &nullEntry, NULL},
      {0x1401, OTYPE_NULL, 0, &nullEntry, NULL},
      {0x1402, OTYPE_NULL, 0, &nullEntry, NULL},
      {0x1403, OTYPE_NULL, 0, &nullEntry, NULL},
      {0x1800, OTYPE_NULL, 0, &nullEntry, NULL},
      {0x1801, OTYPE_NULL, 0, &nullEntry, NULL},
      {0x1802, OTYPE_NULL, 0, &nullEntry, NULL},
      {0x1803, OTYPE_NULL, 0, &nullEntry, NULL},
      {0x7000, OTYPE_VAR, 0, &statusWord, NULL},
      {0x7100, OTYPE_VAR, 0, &actualPosition, NULL},
      {0x7200, OTYPE_VAR, 0, &actualTorque, NULL},
      {0x8000, OTYPE_VAR, 0, &controlWord, NULL},
      {0x8100, OTYPE_VAR, 0, &targetPosition, NULL},
      {0, OTYPE_NULL, 0, NULL, NULL},
   };

   const amp_slave_t slaves[] = {
      {1},
   };

   int8_t syncType = 0;

   cfg.node    = node;
   cfg.bitrate = bitrate;
   cfg.od      = od_none;

   net = co_init (canif, &cfg);
   if (net == NULL)
   {
      printf ("Init failed\n");
      return -1;
   }

   client = co_client_init (net);
   if (client == NULL)
   {
      printf ("Client init failed\n");
      return -1;
   }

   co_nmt (client, CO_NMT_RESET_COMMUNICATION, 0);
   os_usleep (500 * 1000); /* TODO: how to sync with slave responses? */

   node = co_node_next (client, 0);
   if (node == 0)
   {
      printf ("No nodes found\n");
      return -1;
   }

   printf ("%d nodes found\n", co_node_count (client));
   while (node > 0)
   {
      printf ("%d found\n", node);
      char s[80];
      int n;

      n = co_sdo_read (client, node, 0x1008, 0, s, sizeof (s));
      if (n > 0)
      {
         s[n] = 0;
         printf ("(%d) %s\n", node, s);
      }

      n = co_sdo_read (client, node, 0x1009, 0, s, sizeof (s));
      if (n > 0)
      {
         s[n] = 0;
         printf ("(%d) %s\n", node, s);
      }

      n = co_sdo_read (client, node, 0x100a, 0, s, sizeof (s));
      if (n > 0)
      {
         s[n] = 0;
         printf ("(%d) %s\n\n", node, s);
      }

      node = co_node_next (client, node + 1);
   }

   co_nmt (client, CO_NMT_PRE_OPERATIONAL, 0);

   uint8_t u8Val, ix;
   uint16_t u16Val;
   uint32_t u32Val;

   // start canopen layer
   for (ix = 0; ix < NELEMENTS (slaves); ++ix)
   {
      // heartbeat
      {
         // master, 20ms
         u32Val = (slaves[ix].node << 16) | 20;
         co_od1016_fn (net, OD_EVENT_WRITE, NULL, NULL, ix + 1, &u32Val);

         // slave, 5ms
         u16Val = 5;
         co_sdo_write (client, slaves[ix].node, 0x1017, 0, &u16Val, sizeof (u16Val));
      }

      // SYNC
      syncType = 1;
      if (syncType == 0) // master generated
      {
         u32Val = 0x40000080;
         co_od1005_fn (net, OD_EVENT_WRITE, NULL, NULL, 0, &u32Val);
         u32Val = 2000;
         co_od1006_fn (net, OD_EVENT_WRITE, NULL, NULL, 0, &u32Val);
      }
      else if (syncType > 0) // slave generated
      {
         u32Val = 0x40000080;
         co_sdo_write (client, 1, 0x1005, 0, &u32Val, sizeof (u32Val));
         u32Val = 2000;
         co_sdo_write (client, 1, 0x1006, 0, &u32Val, sizeof (u32Val));
      }
      else // none
      {
      }

      // PDO, slave->master
      {
         // master
         co_od1400_fn (net, OD_EVENT_RESTORE, &od_none[0], NULL, 0, NULL);
         u32Val = 0;
         co_od1600_fn (net, OD_EVENT_WRITE, &od_none[0], NULL, 0, &u32Val);
         u32Val = 0x70000010;
         co_od1600_fn (net, OD_EVENT_WRITE, &od_none[0], NULL, 1, &u32Val);
         u32Val = 0x71000020;
         co_od1600_fn (net, OD_EVENT_WRITE, &od_none[0], NULL, 2, &u32Val);
         u32Val = 0x72000010;
         co_od1600_fn (net, OD_EVENT_WRITE, &od_none[0], NULL, 3, &u32Val);
         u32Val = 3;
         co_od1600_fn (net, OD_EVENT_WRITE, &od_none[0], NULL, 0, &u32Val);
         u32Val = 0x00000180 + slaves[ix].node;
         co_od1400_fn (net, OD_EVENT_WRITE, &od_none[0], NULL, 1, &u32Val);

         // slave, TxPDO1
         u32Val = 0x80000180 + slaves[ix].node; // COB-ID disable
         co_sdo_write (client, slaves[ix].node, 0x1800, 1, &u32Val, sizeof (u32Val));
         u8Val = 0; // mapping disable
         co_sdo_write (client, slaves[ix].node, 0x1a00, 0, &u8Val, sizeof (u8Val));
         u32Val = 0x60410010; // status word
         co_sdo_write (client, slaves[ix].node, 0x1a00, 1, &u32Val, sizeof (u32Val));
         u32Val = 0x60640020; // actual position
         co_sdo_write (client, slaves[ix].node, 0x1a00, 2, &u32Val, sizeof (u32Val));
         u32Val = 0x60770010; // actual torque
         co_sdo_write (client, slaves[ix].node, 0x1a00, 3, &u32Val, sizeof (u32Val));
         u8Val = 3; // mapping enable
         co_sdo_write (client, slaves[ix].node, 0x1a00, 0, &u8Val, sizeof (u8Val));
         if (syncType >= 0)
         {
            u8Val = 0x01; // Transmit Type
            co_sdo_write (client, slaves[ix].node, 0x1800, 2, &u8Val, sizeof (u8Val));
         }
         else
         {
            u8Val = 0xFF; // Transmit Type
            co_sdo_write (client, slaves[ix].node, 0x1800, 2, &u8Val, sizeof (u8Val));
            u16Val = 20; // Inhibit timer
            co_sdo_write (client, slaves[ix].node, 0x1800, 3, &u16Val, 2);
            u16Val = 2; // Event timer
            co_sdo_write (client, slaves[ix].node, 0x1800, 5, &u16Val, 2);
         }
         u32Val = 0x00000180 + slaves[ix].node; // COB-ID enable
         co_sdo_write (client, slaves[ix].node, 0x1800, 1, &u32Val, sizeof (u32Val));

         // disable other slave TxPDOs
         u32Val = 0x80000280 + slaves[ix].node;
         co_sdo_write (client, slaves[ix].node, 0x1801, 1, &u32Val, sizeof (u32Val));
         u32Val = 0x80000380 + slaves[ix].node;
         co_sdo_write (client, slaves[ix].node, 0x1802, 1, &u32Val, sizeof (u32Val));
         u32Val = 0x80000480 + slaves[ix].node;
         co_sdo_write (client, slaves[ix].node, 0x1803, 1, &u32Val, sizeof (u32Val));
      }

      // PDO, master->slave
      {
         // master
         co_od1800_fn (net, OD_EVENT_RESTORE, &od_none[4], NULL, 0, NULL);
         u32Val = 0;
         co_od1A00_fn (net, OD_EVENT_WRITE, &od_none[4], NULL, 0, &u32Val);
         u32Val = 0x80000010;
         co_od1A00_fn (net, OD_EVENT_WRITE, &od_none[4], NULL, 1, &u32Val);
         u32Val = 0x81000020;
         co_od1A00_fn (net, OD_EVENT_WRITE, &od_none[4], NULL, 2, &u32Val);
         u32Val = 2;
         co_od1A00_fn (net, OD_EVENT_WRITE, &od_none[4], NULL, 0, &u32Val);
         if (syncType >= 0)
         {
            u32Val = 1;
            co_od1800_fn (net, OD_EVENT_WRITE, &od_none[4], NULL, 2, &u32Val);
         }
         else
         {
            u32Val = 0xff;
            co_od1800_fn (net, OD_EVENT_WRITE, &od_none[4], NULL, 2, &u32Val);
            u32Val = 20;
            co_od1800_fn (net, OD_EVENT_WRITE, &od_none[4], NULL, 3, &u32Val);
            u32Val = 2;
            co_od1800_fn (net, OD_EVENT_WRITE, &od_none[4], NULL, 5, &u32Val);
         }
         u32Val = 0x00000200 + slaves[ix].node;
         co_od1800_fn (net, OD_EVENT_WRITE, &od_none[4], NULL, 1, &u32Val);

         // slave, RxPDO1
         u32Val = 0x80000200 + slaves[ix].node; // COB-ID disable
         co_sdo_write (client, slaves[ix].node, 0x1400, 1, &u32Val, sizeof (u32Val));
         u8Val = 0; // mapping disable
         co_sdo_write (client, slaves[ix].node, 0x1600, 0, &u8Val, sizeof (u8Val));
         u32Val = 0x60400010; // control word
         co_sdo_write (client, slaves[ix].node, 0x1600, 1, &u32Val, sizeof (u32Val));
         u32Val = 0x607a0020; // target position
         co_sdo_write (client, slaves[ix].node, 0x1600, 2, &u32Val, sizeof (u32Val));
         u8Val = 2; // mapping enable
         co_sdo_write (client, slaves[ix].node, 0x1600, 0, &u8Val, sizeof (u8Val));
         u32Val = 0x00000200 + slaves[ix].node; // COB-ID enable
         co_sdo_write (client, slaves[ix].node, 0x1400, 1, &u32Val, sizeof (u32Val));

         // disable other slave RxPDOs
         u32Val = 0x80000300 + slaves[ix].node;
         co_sdo_write (client, slaves[ix].node, 0x1401, 1, &u32Val, sizeof (u32Val));
         u32Val = 0x80000400 + slaves[ix].node;
         co_sdo_write (client, slaves[ix].node, 0x1402, 1, &u32Val, sizeof (u32Val));
         u32Val = 0x80000500 + slaves[ix].node;
         co_sdo_write (client, slaves[ix].node, 0x1403, 1, &u32Val, sizeof (u32Val));
      }
   }

   co_nmt (client, CO_NMT_OPERATIONAL, 0);
   printf ("%d\n", co_nmt_check (client, 0));
   os_usleep (100000);
   printf ("%d\n", co_nmt_check (client, 0));
   printf ("%d\n", co_node_check (client, 2));

   // run canopen layer
   os_tick_t ref = os_tick_current();
   uint32_t step = 2000;
   uint32_t cnt  = 0;
   while (cnt < 2500)
   {
      os_tick_t now = os_tick_current();
      if (co_is_expired (now, ref, step))
      {
         // enabling
         if (cnt > 2000)
         {
            pdoVal.controlWord = 0;
            cnt++;
         }
         else if ((pdoVal.statusWord & 0x4F) == 0x40)
         {
            pdoVal.controlWord = 6;
         }
         else if ((pdoVal.statusWord & 0x6F) == 0x21)
         {
            pdoVal.controlWord = 7;
         }
         else if ((pdoVal.statusWord & 0x6F) == 0x23)
         {
            pdoVal.controlWord = 15;
         }

         // simple motion
         if ((pdoVal.statusWord & 0x6F) == 0x27)
         {
            pdoVal.targetPosition += 5;
            cnt++;
         }
         else
         {
            pdoVal.targetPosition = pdoVal.actualPosition;
         }

         printf (
            "%lu.%lu: %04x, %d, %d | %04x, %d | %d\n",
            now / 1000000000,
            now % 1000000000,
            pdoVal.statusWord,
            pdoVal.actualPosition,
            pdoVal.actualTorque,
            pdoVal.controlWord,
            pdoVal.targetPosition,
            cnt);
         step += 2000;
      }
      else
      {
         os_usleep (100);
      }
   }

   // stop canopen layer
   for (ix = 0; ix < NELEMENTS (slaves); ++ix)
   {
      // SYNC
      if (syncType == 0) // master generated
      {
         co_od1005_fn (net, OD_EVENT_RESTORE, NULL, NULL, 0, NULL);
         co_od1006_fn (net, OD_EVENT_RESTORE, NULL, NULL, 0, NULL);
      }
      else if (syncType > 0) // slave generated
      {
         u32Val = 0x00000080;
         co_sdo_write (client, 1, 0x1005, 0, &u32Val, sizeof (u32Val));
      }
      else // none
      {
      }

      // PDO, slave->master
      {
         u32Val = 0x80000180 + slaves[ix].node;
         co_sdo_write (client, slaves[ix].node, 0x1800, 1, &u32Val, sizeof (u32Val));
      }

      // PDO, master->slave
      {
         u32Val = 0x80000200 + slaves[ix].node;
         co_sdo_write (client, slaves[ix].node, 0x1400, 1, &u32Val, sizeof (u32Val));
      }

      // heartbeat
      {
         // slave, 0ms
         u16Val = 0;
         co_sdo_write (client, slaves[ix].node, 0x1017, 0, &u16Val, sizeof (u16Val));
      }
   }

   return 0;
}
