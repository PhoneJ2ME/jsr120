/*
 * Copyright  1990-2007 Sun Microsystems, Inc. All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 only, as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License version 2 for more details (a copy is
 * included at /legal/license.txt).
 * 
 * You should have received a copy of the GNU General Public License
 * version 2 along with this work; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 * 
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa
 * Clara, CA 95054 or visit www.sun.com if you need additional
 * information or have any questions.
 */

/**
 * @file
 *
 * JSR120 interface methods.
 */

#include <jsr120_sms_protocol.h>
#include <jsr120_cbs_protocol.h>
#include <jsr120_sms_listeners.h>
#if (ENABLE_JSR_205)
#include <jsr205_mms_protocol.h>
#endif

#include <wmaInterface.h>
#include <push_server_resource_mgmt.h>
#include <midpServices.h>
#include <stdio.h>
#include <string.h>

/**
 * Defined in wmaUDPEmulator.c
 * No need in current implementation.
 */
WMA_STATUS init_jsr120() {
    return WMA_NET_SUCCESS;
}

/**
 * Defined in wmaUDPEmulator.c
 * No need in current implementation.
 */
void finalize_jsr120() {
}

// see below
static int checkfilter(char *filter, char *ip);

/**
 * Defined in wmaSocket.c
 * Called from midp loop on WMA signal.
 */
jboolean jsr120_check_signal(midpSignalType signalType, int descriptor, int status) {
    char *filter = NULL;

    switch (signalType) {
    case WMA_SMS_READ_SIGNAL:
    {
        SmsMessage* sms = (SmsMessage*)descriptor;
        filter = pushgetfilter("sms://:", sms->destPortNum);
        if (filter == NULL || checkfilter(filter, sms->msgAddr)) {
            pushsetcachedflag("sms://:", sms->destPortNum);

            jsr120_notify_incoming_sms(
                sms->encodingType, sms->msgAddr, sms->msgBuffer, sms->msgLen,
                sms->sourcePortNum, sms->destPortNum, sms->timeStamp);
            jsr120_sms_delete_msg(sms); //delete in javacall mem
        }
        return KNI_TRUE;
    }
    case WMA_SMS_WRITE_SIGNAL:
        jsr120_notify_sms_send_completed((int)descriptor, (WMA_STATUS)status);
        return KNI_TRUE;
    case WMA_CBS_READ_SIGNAL:
    {
        CbsMessage* cbs = (CbsMessage*)descriptor;
        pushsetcachedflag("cbs://:", cbs->msgID);

	jsr120_notify_incoming_cbs(
            cbs->encodingType, cbs->msgID, cbs->msgBuffer, cbs->msgLen);
        jsr120_cbs_delete_msg(cbs); //delete in javacall mem
        return KNI_TRUE;
    }
#if (ENABLE_JSR_205)
    case WMA_MMS_READ_SIGNAL:
    {
        MmsMessage* mms = (MmsMessage*)descriptor;
        filter = pushgetfiltermms("mms://:", mms->appID);
        if (filter == NULL || checkfilter(filter, mms->replyToAppID)) {
            pushsetcachedflagmms("mms://:", mms->appID);

            //if mms available notification only
            if (mms->msgLen == -1) {
                //mms->data contains javacall_handler for this case
                jsr205_notify_mms_available((int)mms->msgBuffer, mms->appID);
            } else {
                jsr205_notify_incoming_mms(mms->fromAddress, mms->appID,
                    mms->replyToAppID, mms->msgLen, mms->msgBuffer);
            }
            jsr205_mms_delete_msg(mms); //delete in javacall mem
        }
        return KNI_TRUE;
    }
    case WMA_MMS_WRITE_SIGNAL:

        jsr205_mms_notify_send_completed((int)descriptor, (WMA_STATUS)status);
    	return KNI_TRUE;
#endif
    }

    return KNI_FALSE;
}

/**
 * check the SMS header against the push filter.
 * @param filter The filter string to be used
 * @param cmsidn The caller's MSIDN number to be tested by the filter
 * @return <code>1</code> if the comparison is successful; <code>0</code>,
 *     otherwise.
 */
static int checkfilter(char *filter, char *cmsidn) {
    char *p1 = NULL;
    char *p2 = NULL;

#if REPORT_LEVEL <= LOG_INFORMATION
    if (filter != NULL && cmsidn != NULL) {
        reportToLog(LOG_INFORMATION, LC_PROTOCOL,
                    "in checkfilter[%s , %s]",
                    filter, cmsidn);
    }
#endif
    if ((cmsidn == NULL) || (filter == NULL)) return 0;

    /* Filter is exactly "*", then all MSIDN numbers are allowed. */
    if (strcmp(filter, "*") == 0) return 1;

    /*
     * Otherwise walk through the filter string looking for character
     * matches and wildcard matches.
     * The filter pointer is incremented in the main loop and the
     * MSIDN pointer is incremented as characters and wildcards
     * are matched. Checking continues until there are no more filter or
     * MSIDN characters available.
     */
    for (p1=filter, p2=cmsidn; *p1 && *p2; p1++) {
        /*
         * For an asterisk, consume all the characters up to
         * a matching next character.
         */
        if (*p1 == '*') {
            /* Initialize the next two filter characters. */
            char f1 = *(p1+1);
            char f2 = '\0';
            if (f1 != '\0') {
                f2 = *(p1+2);
            }

            /* Skip multiple wild cards. */
            if (f1 == '*') {
                continue;
            }

            /*
             * Consume all the characters up to a match of the next
             * character from the filter string. Stop consuming
             * characters, if the address is fully consumed.
             */
            while (*p2) {
                /*
                 * When the next character matches, check the second character
                 * from the filter string. If it does not match, continue
                 * consuming characters from the address string.
                 */
                if(*p2 == f1 || f1 == '?') {
                    if (*(p2+1) == f2 || f2 == '?' || f2 == '*') {
                        /* Always consume an address character. */
                        p2++;
                        if (f2 != '?' || *(p2+1) == '.' || *(p2+1) == '\0') {
                            /* Also, consume a filter character. */
                            p1++;
                        }
                        break;
                    }
                }
                p2++;
            }
        } else if (*p1 == '?') {
            p2 ++;
        } else if (*p1 != *p2) {
            /* If characters do not match, filter failed. */
            return 0;
        } else {
            p2 ++;
 	}
    }

    if (!(*p1)  && !(*p2) ) {
        /* 
         * All available filter and MSIDN characters were checked.
         */
        return 1;
    } else {
        /*
         * Mismatch in length of filter and MSIDN string
         */
        return 0;
    }
}
