/* Cydia - iPhone UIKit Front-End for Debian APT
 * Copyright (C) 2008-2015  Jay Freeman (saurik)
*/

/* GNU General Public License, Version 3 {{{ */
/*
 * Cydia is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 3 of the License,
 * or (at your option) any later version.
 *
 * Cydia is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Cydia.  If not, see <http://www.gnu.org/licenses/>.
**/
/* }}} */

#include "CyteKit/UCPlatform.h"

#include "CyteKit/stringWith.h"

@implementation NSString (Cyte)

+ (NSString *) stringWithUTF8BytesNoCopy:(const char *)bytes length:(int)length {
    return [[[NSString alloc] initWithBytesNoCopy:const_cast<char *>(bytes) length:length encoding:NSUTF8StringEncoding freeWhenDone:NO] autorelease];
}

+ (NSString *) stringWithUTF8Bytes:(const char *)bytes length:(int)length {
    return [[[NSString alloc] initWithBytes:bytes length:length encoding:NSUTF8StringEncoding] autorelease];
}

+ (NSString *) stringWithFormat:(NSString *)format :(size_t)count :(id *)args {
    switch (count) {
        case 0:
            return [[[NSString alloc] initWithFormat:format] autorelease];;
        case 1:
            return [[[NSString alloc] initWithFormat:format, args[0]] autorelease];;
        case 2:
            return [[[NSString alloc] initWithFormat:format, args[0], args[1]] autorelease];;
        case 3:
            return [[[NSString alloc] initWithFormat:format, args[0], args[1], args[2]] autorelease];;
        case 4:
            return [[[NSString alloc] initWithFormat:format, args[0], args[1], args[2], args[3]] autorelease];;
        case 5:
            return [[[NSString alloc] initWithFormat:format, args[0], args[1], args[2], args[3], args[4]] autorelease];;
        case 6:
            return [[[NSString alloc] initWithFormat:format, args[0], args[1], args[2], args[3], args[4], args[5]] autorelease];;
        case 7:
            return [[[NSString alloc] initWithFormat:format, args[0], args[1], args[2], args[3], args[4], args[5], args[6]] autorelease];;
        case 8:
            return [[[NSString alloc] initWithFormat:format, args[0], args[1], args[2], args[3], args[4], args[5], args[6], args[7]] autorelease];;
        default:
            _assert(false);
    }
}

@end
