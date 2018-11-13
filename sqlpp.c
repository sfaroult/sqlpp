/*
 *   SQL preprocessor
 *
 *   S. Faroult
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#define OPTIONS "?hdD:"

#define LINELEN     2048
#define STACKSZ      100
#define SYMBOL_LEN    64

#define TOK_PAR        0
#define TOK_VAL        1
#define TOK_OP         2
#define TOK_DONE       3
#define TOK_ERROR      4

typedef struct token {
       char type;
       char val;
     } TOKEN;

typedef struct symbol {
         char *symb;
         char *v;      // For future enhancements
         char  flag;   // To remove it logically - far simpler
                       // than doing it physically
         struct symbol *left;
         struct symbol *right;
        } SYMBOL;

static char    G_debug = 0;
static SYMBOL *G_symbols = NULL;
static char    G_stack[STACKSZ];
static short   G_stackidx = 0;

static char must_output(void) {
   short i;
   char  ok = 1;

   for (i = 0; i < G_stackidx; i++) {
     ok &= G_stack[i];
   }
   return ok;
}

static void add_symbol(SYMBOL **symbp, char *s) {
    if (symbp && s) {
      if (*symbp == NULL) {
        char *v;

        if ((*symbp = (SYMBOL *)malloc(sizeof(SYMBOL))) != NULL) {
          if ((v = strchr(s, '=')) != NULL) {
            *v++ = '\0';
          }
          (*symbp)->symb = strdup(s);
          if (v) {
            (*symbp)->v = strdup(v);
          } else {
            (*symbp)->v = NULL;
          }
          (*symbp)->flag = 0;
          (*symbp)->left = NULL;
          (*symbp)->right = NULL;
        } else {
          perror("malloc()");
          exit(1);
        }
      } else {
        int cmp = strcasecmp((*symbp)->symb, s);
        if (0 == cmp) {
          if ((*symbp)->flag) {
            (*symbp)->flag = 0; // Was deleted
          }
        }
        if (cmp < 0) {
          add_symbol(&((*symbp)->right), s);
        } else if (cmp > 0) {
          add_symbol(&((*symbp)->left), s);
        }
      }
    }
}

static char remove_symbol(SYMBOL **symbp, char *s) {
    // Returns 1 if OK, 0 if not found
    // Just flagged, no physical deletion
    char ret = 0;
    if (symbp && *symbp && s) {
      int cmp = strcasecmp((*symbp)->symb, s);
      if (0 == cmp) {
        if ((*symbp)->flag) { // Already deleted = not found
          ret = 0;
        } else {
          (*symbp)->flag = 1;
          ret = 1;
        }
      } else if (cmp < 0) {
        ret = remove_symbol(&((*symbp)->right), s);
      } else if (cmp > 0) {
        ret = remove_symbol(&((*symbp)->left), s);
      }
    }
    return ret;
}

static void free_symbols(SYMBOL **symbp) {
    if (symbp) {
      if (*symbp != NULL) {
        free_symbols(&((*symbp)->right));
        free_symbols(&((*symbp)->left));
        if ((*symbp)->v) {
          free((*symbp)->v);
        }
        if ((*symbp)->symb) {
          if (G_debug) {
            fprintf(stderr, "Freeing %s\n", (*symbp)->symb);
          }
          free((*symbp)->symb);
        }
        free(*symbp);
        *symbp = NULL;
      }
    }
}

static char symbol_defined(SYMBOL *symbols, char *symb) {
    if (symbols && symb) {
      int cmp = strcasecmp(symbols->symb, symb);
      if (G_debug) {
        fprintf(stderr, "Comparing %s (searched) to %s\n",
                         symb, symbols->symb);
      }
      if (cmp == 0) {
        if (symbols->flag) {
          return 0;
        } else {
          return 1;
        }
      } else if (cmp < 0) {
        return symbol_defined(symbols->right, symb);
      } else {
        return symbol_defined(symbols->left, symb);
      }
    }
    return 0;
}

static char *symbol_value(SYMBOL *symbols, char *symb) {
    if (symbols && symb) {
      int cmp = strcasecmp(symbols->symb, symb);
      if (cmp == 0) {
        return symbols->v;
      } else if (cmp < 0) {
        return symbol_value(symbols->right, symb);
      } else {
        return symbol_value(symbols->left, symb);
      }
    }
    return NULL;
}

static TOKEN read_token(char *expr) {
    static char  *e = NULL;
           char   symbol[SYMBOL_LEN];
           short  s = 0;
           char   in_symb = 0;
           short  offset;
           TOKEN  ret;

    ret.type = TOK_ERROR;
    if (expr) {
      e = expr;
    }
    if (e) {
      while (*e) {
        switch (*e) {
          case '(':
          case ')':
               if (in_symb) {
                 symbol[s] = '\0';
                 // Check
                 if (G_debug) {
                   fprintf(stderr, "Looking for symbol %s\n", symbol);
                 }
                 ret.val = '0' + symbol_defined(G_symbols, symbol);
                 ret.type = TOK_VAL;
                 // DON'T increment e
                 return ret;
               }
               ret.type = TOK_PAR;
               ret.val = *e;
               e++;
               return ret;
          default :
               if (isspace(*e)) {
                 if (in_symb) {
                   symbol[s] = '\0';
                   in_symb = 0;
                   // Check
                   if (G_debug) {
                     fprintf(stderr, "Looking for symbol %s\n", symbol);
                   }
                   ret.val = '0' + symbol_defined(G_symbols, symbol);
                   ret.type = TOK_VAL;
                   e++;
                   return ret;
                 } // Else ignore
               } else {
                 if (in_symb) {
                   if (isalnum(*e)
                       || (*e == '_')
                       || (*e == '#')
                       || (*e == '$')) {
                     symbol[s++] = *e;
                   } else {
                     // End of symbol
                     symbol[s] = '\0';
                     in_symb = 0;
                     // Check
                     if (G_debug) {
                       fprintf(stderr, "Looking for symbol %s\n", symbol);
                     }
                     ret.val = '0' + symbol_defined(G_symbols, symbol);
                     ret.type = TOK_VAL;
                     return ret;
                   }
                 } else {
                   // Not in a symbol
                   offset = 0;
                   if (strncasecmp(e, "or", 2) == 0) {
                     offset = 2;
                     ret.val = '+';
                   } else if (strncasecmp(e, "and", 3) == 0) {
                     offset = 3;
                     ret.val = '*';
                   }
                   if (offset) {
                     if (isspace(e[offset])
                        || ('(' == e[offset])) {
                       if (G_debug) {
                         fprintf(stderr, "Found operator %c\n", ret.val);
                       }
                       ret.type = TOK_OP;
                       e += offset;
                       return ret; 
                     } else {
                       // Consider it to be the beginning of a new symbol
                       s = 0;
                       symbol[s++] = *e;
                       in_symb = 1;
                     }
                   } else {
                     // New symbol
                     s = 0;
                     symbol[s++] = *e;
                     in_symb = 1;
                   }
                 }
               }
               break;
        }
        e++;
      }
      if (in_symb) {
        symbol[s] = '\0';
        in_symb = 0;
        // Check
        if (G_debug) {
          fprintf(stderr, "Looking for symbol %s\n", symbol);
        }
        ret.val = '0' + symbol_defined(G_symbols, symbol);
        ret.type = TOK_VAL;
      } else {
        ret.type = TOK_DONE;
      }
    }
    return ret;
}

static char precedence(char a, char b) {
    if ((a == '+') && (b == '*')) {
      return 0;
    } else {
      return 1;
    }
}

static char operate(char a, char b, char oper) {
  char res = 0;

  char val1 = a - '0';
  char val2 = b - '0';
  switch (oper) {
    case '+':
         res = val1 + val2;
         if (res > 1) {
           res = 1;
         }
         break;
    case '*':
         res = val1 * val2;
         break;
  }
  return '0' + res;
}

static char evaluate(char *expression, int linenum) {
  TOKEN tok;
  char  stack[STACKSZ];
  char  expr[STACKSZ+1];
  char *e;
  short s = -1;
  char  op_stack[STACKSZ];
  short os = -1;
  char  operand1;
  char  operand2;

  if (expression == NULL) {
    return -1;
  }
  if (G_debug) {
    fprintf(stderr, "Evaluating <%s>\n", expression);
  }
  // First convert to postfix
  // Shunting-yard algorithm, based on Wikipedia article
  //
  tok = read_token(expression);
  while (tok.type != TOK_DONE) {
    if (G_debug) {
      fprintf(stderr, " -- Token type: %d, value: %c\n",
              (int)tok.type,
              tok.val);
    }
    if (tok.type == TOK_VAL) {
      // Push it to the output queue
      stack[++s] = tok.val;
    } else if (tok.type == TOK_OP) {
      // precedence(a,b) must return 1 if a has greater precedence than b
      while ((os >= 0)
             && (s < STACKSZ-1)
             && precedence(op_stack[os], tok.val)
             && (op_stack[os] != '(')) {
        stack[++s] = op_stack[os--]; 
      }
      op_stack[++os] = tok.val;
    } else if (tok.type == TOK_PAR) {
      if (tok.val == '(') {
        if (os < STACKSZ-1) {
          op_stack[++os] = tok.val;
        }
      } else {
        // Right parenthese
        while ((os >= 0)
               && (s < STACKSZ-1)
               && (op_stack[os] != '(')) {
          stack[++s] = op_stack[os--]; 
        }
        if (os < 0) {
          fprintf(stderr, "Mismatched parentheses on line %d\n", linenum);
          return -1;
        }
        if (op_stack[os] == '(') {
          os--;
        }
      }
    }
    tok = read_token(NULL);
  }
  if (os >= 0) {
    if (op_stack[os] == '(') {
      // Mismatched parentheses
      fprintf(stderr, "Mismatched parentheses on line %d\n", linenum);
      return -1;
    } else {
      while ((os >= 0) && (s < STACKSZ)) {
        stack[++s] = op_stack[os--]; 
      }
    }
  }
  if (s > 0) {
    memcpy(expr, stack, s+1);
    expr[s+1] = '\0';
    if (G_debug) {
      fprintf(stderr, "Evaluation stack: %s\n", expr);
    }
    // Now evaluate the expression
    s = -1;
    e = expr;
    while (*e) {
      if (isdigit(*e) && (s < STACKSZ)) {
        stack[++s] = *e;
      } else {
        if (s) {
          operand1 = stack[s--];
          operand2 = stack[s--];
          stack[++s] = operate(operand1, operand2, *e);
        }
      }
      e++;
    }
  }
  if (G_debug) {
    fprintf(stderr, "Expression evaluates to: %c\n", stack[s]);
  }
  return stack[s] - '0';
}

int main(int argc, char **argv) {
    FILE   *fp = NULL;
    int     ch;
    char    close = 0;
    char    line[LINELEN];
    char   *p;
    char   *q;
    char    output = 1;
    int     len;
    int     linecnt = 0;
    char    result;

    while ((ch = getopt(argc, argv, OPTIONS)) != -1) {
      switch (ch) {
        case '?' :
        case 'h' :
             fprintf(stdout,
                     "sqlpp is a utility for pre-processing SQL scripts.\n");
             fprintf(stdout,
             "Its purpose is to allow multiple DBMS-specific DDL syntax inside"
                     " a single script.\n");
             fprintf(stdout,
                     "Usage: %s -D symbol [ -D symbol ...]\n", argv[0]);
             fprintf(stdout,
                     "Symbols are simple tags that comment/uncomment "
                     "lines in an SQL script.\n");
             fprintf(stdout,
                     "In the script control is performed using:\n");
             fprintf(stdout, "--ifdef <symbol expression>\n");
             fprintf(stdout, "--ifndef <symbol expression>\n");
             fprintf(stdout, "--else\n");
             fprintf(stdout, "--endif\n");
             fprintf(stdout,
                 "\nA symbol expression is a single symbol or a logical\n");
             fprintf(stdout,
              "expression combining symbols with AND, OR, and parentheses.\n");
             fprintf(stdout,
                     "\nSymbols can also be controlled in the script using:\n");
             fprintf(stdout, "--define <symbol>\n");
             fprintf(stdout, "--undef <symbol>\n");
             fprintf(stdout,
                     "\nLines that can be uncommented must start with --#\n");
             fprintf(stdout,
                     "(regular comments are left untouched)\n");
             exit(0);
             break; // Not reached
        case 'd' :
             G_debug = 1;
             fprintf(stderr, "--- Debug mode on\n");
             break;
        case 'D':
             add_symbol(&G_symbols, optarg);
             break;
        default:
             fprintf(stderr, "Invalid option -%c\n", ch);
             exit(-1);
      }
    }
    if (optind == argc) {
      // Read from stdin
      fp = stdin;
    } else {
      if ((fp = fopen(argv[optind], "r")) == NULL) {
        perror(argv[optind]);
        free_symbols(&G_symbols);
        exit(1);
      }
      close = 1;
    }
    while (fgets(line, LINELEN, fp)) {
      p = line;
      linecnt++;
      while (isspace(*p)) {
        p++;
      }
      if (strncmp(p, "--", 2)) {
        if (output) {
          printf("%s", line);
        }
      } else {
        // Comment indeed, but is it a preprocessor comment?
        if (strncasecmp(p, "--ifdef", 7) == 0) {
          if (G_stackidx < STACKSZ) {
            p += 7;
            while (isspace(*p)) {
              p++;
            }
            G_stack[G_stackidx] = evaluate(p, linecnt);
            if (G_stack[G_stackidx] != -1) {
              G_stackidx++;
              output = must_output();
            }
          } else {
            fprintf(stderr, "ERROR: stack overflow line %d\n", linecnt);
          }
        } else if (strncasecmp(p, "--ifndef", 8) == 0) {
          if (G_stackidx < STACKSZ) {
            p += 8;
            while (isspace(*p)) {
              p++;
            }
            G_stack[G_stackidx] = !evaluate(p, linecnt);
            if (G_stack[G_stackidx] != -1) {
              G_stackidx++;
              output = must_output();
            }
          } else {
            fprintf(stderr, "ERROR: stack overflow line %d\n", linecnt);
          }
        } else if (strncasecmp(p, "--else", 6) == 0) {
          if (G_stackidx > 0) {
            G_stack[G_stackidx-1] = !G_stack[G_stackidx-1];
            output = must_output();
          } else {
            fprintf(stderr, "ERROR: --else outside of condition line %d\n",
                            linecnt);
          }
        } else if (strncasecmp(p, "--endif", 7) == 0) {
          if (G_stackidx == 0) {
            fprintf(stderr, "ERROR: --endif without condition start line %d\n",
                            linecnt);
          } else {
            G_stackidx--;
            output = must_output();
          }
        } else if (strncasecmp(p, "--define", 8) == 0) {
            p += 8;
            while (isspace(*p)) {
              p++;
            }
            len = strlen(p);
            while (len && isspace(p[len-1])) {
              len--;
            } 
            if (len) {
              p[len] = '\0';
              add_symbol(&G_symbols, p);
            } else {
              fprintf(stderr, "ERROR: --define followed by nothing line %d\n",
                              linecnt);
            }
        } else if (strncasecmp(p, "--undef", 7) == 0) {
            char ok;

            p += 7;
            while (isspace(*p)) {
              p++;
            }
            len = strlen(p);
            while (len && isspace(p[len-1])) {
              len--;
            } 
            if (len) {
              p[len] = '\0';
              ok = remove_symbol(&G_symbols, p);
              if (!ok) {
                fprintf(stderr, "ERROR: --undef non existing symbol line %d\n",
                              linecnt);
              }
            } else {
              fprintf(stderr, "ERROR: --undef followed by nothing line %d\n",
                              linecnt);
            }
        } else {
          // Nyet
          if (output) {
            if (strncasecmp(p, "--#", 3) == 0) {
              // Remove the comment and print
              p += 3;
              // Let three spaces if there was some indentation
              printf("   %s", p);
            } else {
              printf("%s", line);
            }
          }
        }
      }
    }
    if (G_stackidx) {
      fprintf(stderr, "ERROR: missing --endif line %d\n",
                      linecnt);
    }
    free_symbols(&G_symbols);
    if (fp && close) {
      fclose(fp);
    }
    return 0;
}
