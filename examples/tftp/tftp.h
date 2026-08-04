#ifndef TFTP_H
#define TFTP_H

#include <hammer/hammer.h>

HParser *rrqwrqParser(void);
HParser *dataParser(void);
HParser *ackParser(void);
HParser *errpktParser(void);

#endif
