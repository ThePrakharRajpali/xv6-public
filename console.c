// Console input and output.
// Input is from the keyboard or serial port.
// Output is written to the screen and serial port.
#include "console.h"
#include "types.h"
#include "defs.h"
#include "param.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"

static void consputc(int);

static int panicked = 0;

static struct
{
  struct spinlock lock;
  int locking;
} cons;

static void
printint(int xx, int base, int sign)
{
  static char digits[] = "0123456789abcdef";
  char buf[16];
  int i;
  uint x;

  if (sign && (sign = xx < 0))
    x = -xx;
  else
    x = xx;

  i = 0;
  do
  {
    buf[i++] = digits[x % base];
  } while ((x /= base) != 0);

  if (sign)
    buf[i++] = '-';

  while (--i >= 0)
    consputc(buf[i]);
}
//PAGEBREAK: 50

// Print to the console. only understands %d, %x, %p, %s.
void cprintf(char *fmt, ...)
{
  int i, c, locking;
  uint *argp;
  char *s;

  locking = cons.locking;
  if (locking)
    acquire(&cons.lock);

  if (fmt == 0)
    panic("null fmt");

  argp = (uint *)(void *)(&fmt + 1);
  for (i = 0; (c = fmt[i] & 0xff) != 0; i++)
  {
    if (c != '%')
    {
      consputc(c);
      continue;
    }
    c = fmt[++i] & 0xff;
    if (c == 0)
      break;
    switch (c)
    {
    case 'd':
      printint(*argp++, 10, 1);
      break;
    case 'x':
    case 'p':
      printint(*argp++, 16, 0);
      break;
    case 's':
      if ((s = (char *)*argp++) == 0)
        s = "(null)";
      for (; *s; s++)
        consputc(*s);
      break;
    case '%':
      consputc('%');
      break;
    default:
      // Print unknown % sequence to draw attention.
      consputc('%');
      consputc(c);
      break;
    }
  }

  if (locking)
    release(&cons.lock);
}

void panic(char *s)
{
  int i;
  uint pcs[10];

  cli();
  cons.locking = 0;
  // use lapiccpunum so that we can call panic from mycpu()
  cprintf("lapicid %d: panic: ", lapicid());
  cprintf(s);
  cprintf("\n");
  getcallerpcs(&s, pcs);
  for (i = 0; i < 10; i++)
    cprintf(" %p", pcs[i]);
  panicked = 1; // freeze other CPU
  for (;;)
    ;
}

//PAGEBREAK: 50
static ushort *crt = (ushort *)P2V(0xb8000); // CGA memory

// Prints character on screen
static void
cgaputc(int c)
{
  int pos;

  // Cursor position: col + 80*row.
  outb(CRTPORT, 14);
  pos = inb(CRTPORT + 1) << 8;
  outb(CRTPORT, 15);
  pos |= inb(CRTPORT + 1);

  if (c == '\n')
    pos += 80 - pos % 80;
  else if (c == BACKSPACE)
  {
    if (pos > 0)
      --pos;
  }
  else if (c == LEFT_ARROW)
  {
    if (pos > 0)
      --pos;
  }
  else
    crt[pos++] = (c & 0xff) | 0x0700; // black on white

  if (pos < 0 || pos > 25 * 80)
    panic("pos under/overflow");

  if ((pos / 80) >= 24)
  { // Scroll up.
    memmove(crt, crt + 80, sizeof(crt[0]) * 23 * 80);
    pos -= 80;
    memset(crt + pos, 0, sizeof(crt[0]) * (24 * 80 - pos));
  }

  outb(CRTPORT, 14);
  outb(CRTPORT + 1, pos >> 8);
  outb(CRTPORT, 15);
  outb(CRTPORT + 1, pos);
  if (c == BACKSPACE)
    crt[pos] = ' ' | 0x0700;
}

// Writing on xv6 screen
void consputc(int c)
{
  if (panicked)
  {
    cli();
    for (;;)
      ;
  }

  if (c == BACKSPACE)
  {
    uartputc('\b');
    uartputc(' ');
    uartputc('\b'); //PRINTS ON TERMINAL
  }
  else if (c == LEFT_ARROW)
  {
    uartputc(c);
  }
  else
    uartputc(c);
  cgaputc(c); // PRINTS ON QEMU
}

struct
{
  char buf[INPUT_BUF];
  uint r;         // Read index
  uint w;         // Write index
  uint e;         // Edit index
  uint rightmost; // Position after last character
} input;

char charsToBeMoved[INPUT_BUF]; // FOR STORING INPUT BUFFER FOR SOME FUNCTIONS

#define C(x) ((x) - '@') // Control-x

/*
Copy input.buf to a safe location. Used only when punching in new keys and the
caret isn't at the end of the line.
*/
void copyCharsToBeMoved()
{
  uint n = input.rightmost - input.e;
  uint i;
  for (i = 0; i < n; i++)
    charsToBeMoved[i] = input.buf[input.e + i % INPUT_BUF];
}

/*
Shift input.buf one byte to the right, and repaint the chars on-screen. Used
Used only when punching in new keys and the caret isn't at the end of the line.
*/
void shiftbufright()
{
  uint n = input.rightmost - input.e;
  int i;
  for (i = 0; i < n; i++)
  {
    char c = charsToBeMoved[i];
    input.buf[input.e + i % INPUT_BUF] = c;
    consputc(c);
  }

  memset(charsToBeMoved, '\0', INPUT_BUF);
  // return the caret to its correct position
  for (i = 0; i < n; i++)
  {
    consputc(LEFT_ARROW);
  }
}

/*
Shift input.buf one byte to the left, and repaint the chars on-screen. Used
Used only when punching in BACKSPACE and the caret isn't at the end of the line.
*/
void shiftbufleft()
{
  uint n = input.rightmost - input.e;
  uint i;
  consputc(LEFT_ARROW);
  input.e--;
  for (i = 0; i < n; i++)
  {
    char c = input.buf[input.e + i + 1 % INPUT_BUF];
    input.buf[input.e + i % INPUT_BUF] = c;
    consputc(c);
  }
  input.rightmost--;
  consputc(' '); // delete the last char in line
  for (i = 0; i <= n; i++)
  {
    consputc(LEFT_ARROW); // shift the caret back to the left
  }
}

void consoleintr(int (*getc)(void))
{
  int c, doprocdump = 0;
  uint i, n;
  acquire(&cons.lock);
  while ((c = getc()) >= 0)
  {
    switch (c)
    {
    case C('P'):      // Process listing.
      doprocdump = 1; // procdump() locks cons.lock indirectly; invoke later
      break;
    case C('U'): // Kill line.
      if (input.rightmost > input.e)
      {
        int numtoshift = input.rightmost - input.e;
        uint placestoshift = input.e - input.w;
        uint i;
        for (i = 0; i < placestoshift; i++)
        {
          consputc(LEFT_ARROW);
        }
        memmove(input.buf + input.w, input.buf + input.w + placestoshift, numtoshift); //copying the remaining chars to the beginning
        input.e -= placestoshift;
        input.rightmost -= placestoshift;
        for (i = 0; i < numtoshift; i++)
        { // repaint the chars
          consputc(input.buf[input.e + i % INPUT_BUF]);
        }
        for (i = 0; i < placestoshift; i++)
        { // erase the leftover chars
          consputc(' ');
        }
        for (i = 0; i < placestoshift + numtoshift; i++)
        { // move the caret back to the left
          consputc(LEFT_ARROW);
        }
      }
      else
      {
        while (input.e != input.w &&
               input.buf[(input.e - 1) % INPUT_BUF] != '\n')
        {
          input.e--;
          input.rightmost--;
          consputc(BACKSPACE);
        }
      }
      break;
    case C('H'):
    case '\x7f': // Backspace
      if (input.rightmost != input.e && input.e != input.w)
      { // caret isn't at the end of the line
        shiftbufleft();
        break;
      }
      if (input.e != input.w)
      { // caret is at the end of the line
        input.e--;
        input.rightmost--;
        consputc(BACKSPACE);
      }
      break;
    case LEFT_ARROW:
      if (input.e != input.w)
      {
        input.e--;
        consputc(c);
      }
      break;
    case RIGHT_ARROW:
      if (input.e < input.rightmost)
      {
        n = input.rightmost - input.e;
        for (i = 0; i < n; i++)
        {
          consputc(input.buf[input.e + i % INPUT_BUF]);
        }
        for (i = 0; i < n - 1; i++)
        {
          consputc(LEFT_ARROW);
        }
        input.e++;
        consputc(input.e == input.rightmost ? ' ' : input.buf[input.e]);
        consputc(LEFT_ARROW);
      }
      break;
    case UP_ARROW:
      if (current_history_viewed.historyId < 15)
      {
        earaseCurrentLineOnScreen();
        if (current_history_viewed.historyId == -1)
          copyCharsToBeMovedfToOldBuf();
        earaseContentOnInput_buf();
        current_history_viewed.historyId++;

        history(current_history_viewed.buf, current_history_viewed, &current_history_viewed.length); //GILAD QUES how to make this syscall?!?!

        copyBufferToScreen(current_history_viewed.buf, current_history_viewed.oldBuf.length);
        copyBufferToInputBuf(current_history_viewed.buf, current_history_viewed.oldBuf.length);
      }
      break;
    case DOWN_ARROW:
      //earase the current line on screen
      //earse current input.buf                                                                                              //GILAD
      // if (historyId == 0) {copyOldBufToInputBuf ; historyId-- ; copyOldBufToScreen}  very similar t the next two lines
      //{ copy current_history_viewed.buf  to screen using "void copy_buffer_to_screen"
      // copy  current_history_viewed.buf to input.buf (doing extrawork when going through history)  }

      switch (current_history_viewed.historyId)
      {
      case -1:
        //does nothing
        break;
      case 0: //get string from old buf
        earaseCurrentLineOnScreen();
        copyBufferToInputBuf(current_history_viewed.oldBuf, current_history_viewed.oldBuf.lengthOld);
        copyBufferToScreen(current_history_viewed.oldBuf, current_history_viewed.oldBuf.lengthOld);
        current_history_viewed.historyId--;
        break;

      default:
        earaseCurrentLineOnScreen();
        current_history_viewed.historyId--;

        history(current_history_viewed.buf, current_history_viewed.historyId, &current_history_viewed.length); //GILAD QUES how to make this syscall?!?!

        copyBufferToInputBuf(current_history_viewed.buf current_history_viewed.oldBuf.length);
        copyBufferToScreen(current_history_viewed.buf, current_history_viewed.oldBuf.length);
        break;
      }

      break;
    case '\n':
    case '\r':
      input.e = input.rightmost;
    default:
      if (c != 0 && input.e - input.r < INPUT_BUF)
      {
        c = (c == '\r') ? '\n' : c;
        if (input.rightmost > input.e)
        { // caret isn't at the end of the line
          copybuf();
          input.buf[input.e++ % INPUT_BUF] = c;
          input.rightmost++;
          consputc(c);
          shiftbufright();
        }
        else
        {
          input.buf[input.e++ % INPUT_BUF] = c;
          input.rightmost = input.e - input.rightmost == 1 ? input.e : input.rightmost;
          consputc(c);
        }
        if (c == '\n' || c == C('D') || input.e == input.r + INPUT_BUF)
        {
          input.w = input.e;
          wakeup(&input.r);
        }
      }
      break;
    }
  }
  release(&cons.lock);
  if (doprocdump)
  {
    procdump(); // now call procdump() wo. cons.lock held
  }
}

/*
  this method eareases the current line from screen
*/
void earaseCurrentLineOnScreen(void)
{
  //TODO
}

/*
  this method copies the chars currently on display (and on Input.buf) to current_history_viewed.oldBuf and save its length on current_history_viewed.lengthOld
*/
void copyCharsToBeMovedfToOldBuf(void)
{
  //TODO
}

/*
  this method will print the given buf on the screen
*/
void copyBufferToScreen(char *bufToPrintOnScreen, uint length)
{
  //TODO
}

/*
  this method will copy the given buf to Input.buf
  will set the input.e and input.rightmost
*/
void copyBufferToInputBuf(char *bufToSaveInInput, uint length)
{
  //TODO
}

struct
{
  char buf[INPUT_BUF];
  uint c;
} blaa;
//blaa.c=1;

/*
  this struct will hold the current history command view.                                                                                   GILAD
*/
struct
{
  char bufferArr[MAX_HISTORY][INPUT_BUF]; //holds the actual command strings -
  uint lengthsArr[MAX_HISTORY];           // this will hold the length of each command string
  uint lastCommandIndex;                  //the last command of the history
  uint numOfCommmandsInMem;               //number of history commands in mem
} history_buffer_array;

/*
  this method writes the requested command in the buffer (and its length)
*/
void history(char *buffer, int historyId, int *length)
{
  int indexInArray = (history_buffer_array.lastCommandIndex + historyId) % MAX_MEMORY_COMMAND_IN_HISTORY;
  memmove(buffer, history_buffer_array.bufferArr[indexInArray], history_buffer_array.lengthsArr[indexInArray]);
  *length = history_buffer_array.lengthsArr[indexInArray];
}
int a;

int consoleread(struct inode *ip, char *dst, int n)
{
  uint target;
  int c;

  iunlock(ip);
  target = n;
  acquire(&cons.lock);
  while (n > 0)
  {
    while (input.r == input.w)
    {
      if (myproc()->killed)
      {
        release(&cons.lock);
        ilock(ip);
        return -1;
      }
      sleep(&input.r, &cons.lock);
    }
    c = input.buf[input.r++ % INPUT_BUF];
    if (c == C('D'))
    { // EOF
      if (n < target)
      {
        // Save ^D for next time, to make sure
        // caller gets a 0-byte result.
        input.r--;
      }
      break;
    }
    *dst++ = c;
    --n;
    if (c == '\n')
      break;
  }
  release(&cons.lock);
  ilock(ip);

  return target - n;
}

int consolewrite(struct inode *ip, char *buf, int n)
{
  int i;

  iunlock(ip);
  acquire(&cons.lock);
  for (i = 0; i < n; i++)
    consputc(buf[i] & 0xff);
  release(&cons.lock);
  ilock(ip);

  return n;
}

void consoleinit(void)
{
  initlock(&cons.lock, "console");

  devsw[CONSOLE].write = consolewrite;
  devsw[CONSOLE].read = consoleread;
  cons.locking = 1;

  ioapicenable(IRQ_KBD, 0);

  consputc('Y'); //GILAD DELETE
  consputc('o');
  consputc('o');
  consputc('o');
  consputc('\n');
}
