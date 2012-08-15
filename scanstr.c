#include "scanstr.h"
#include "config.h"

#include <stdint.h>
#include <string.h>

/** An adoption of Railgun_Doublet by Georgi 'Kaze' <sanmayce@sanmayce.com>,
 * See: http://www.sanmayce.com/Railgun/index.html */
const unsigned char* scanstr(const unsigned char *text, int nText, const unsigned char *pattern, int nPattern){
  const unsigned char * pbTarget = text;
  const unsigned char * pbPattern = pattern;
  uint32_t cbTarget = nText;
  uint32_t cbPattern = nPattern;
  if(cbPattern == 0)
    return pbTarget;
  if(cbPattern == 1)
    return memchr(pbTarget, *pbPattern, cbTarget);
	const unsigned char * pbTargetMax = pbTarget + cbTarget;
	register uint32_t ulHashPattern;
	uint32_t ulHashTarget, count, countSTATIC;

  UNUSED_PARAMETER(ulHashTarget);
	if (cbPattern > cbTarget) return(NULL);

	countSTATIC = cbPattern-2;

	pbTarget = pbTarget+cbPattern;
	ulHashPattern = (*(uint16_t *)(pbPattern));

	for ( ;; ) {
		if ( ulHashPattern == (*(uint16_t *)(pbTarget-cbPattern)) ) {
			count = countSTATIC;
			while ( count && *(const unsigned char *)(pbPattern+2+(countSTATIC-count)) == *(const unsigned char *)(pbTarget-cbPattern+2+(countSTATIC-count)) ) {
				count--;
			}
			if ( count == 0 ) return((pbTarget-cbPattern));
		}
		pbTarget++;
		if (pbTarget > pbTargetMax) return(NULL);
	}
}

