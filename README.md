# sqlpp
I had a need for deriving several versions of varying complexity from a same basic database model, and I didn't want to maintain several closely related SQL schema creation SQL scripts; especially as several different DBMS might be used. I thought of using cpp and conditional compilation, but cpp was obviously distressed when fed a .sql file. A glance at m4 convinced me that I'd rather write my own unsophisticated small tool.

**sqlpp** was written to mostly understand `#ifdef`, `#ifndef`, `#else` and `#endif`. To be as unobstrusive as possible, they have been renamed `--ifdef`, `--ifndef`, `--else` and `--endif`. I have also implemented `--define` and `--undef`, but symbols are normally expected to be passed on the command line:

   `sqlpp -D sym1 -D sym2 ...` _script_

Output goes to the standard output. If no filename is provided, input is read from the standard input.

The program only looks at SQL comments that are alone on a line (they need not start in the first column). If it recognizes them, it interprets them, and sets a flag that says whether to output the next lines or not until the next command understood by **sqlpp**. Additionally, a special comment (`--#`) has been defined that is uncommented by **sqlpp** when lines must be output. This allows to have functional scripts even when **sqlpp** is not available.

In true SQL tradition, case is irrelevant with **sqlpp**.

## Syntax in Brief

* `--ifdef` _condition_ Turns output on or off depending on whether the condition is true or not. The condition can be whether a single symbol is defined, or several symbols linked by *`and`* or *`or`* (parentheses are also usable). *`not`* isn't implemented, use `--ifndef`, which negates the condition, instead.
* `--ifndef` _condition_ Negates the condition.
* `--else`
* `--endif`
* `--define` _symbol_ When symbols are dependent on each other
* `--undef` _symbol_ When symbols are dependent on each other
* `--# uncommented when output is turned on`

## A Simple Example

This is the full script:
~~~
create table test (
--ifndef MYSQL or POSTGRES or SQLITE
   id    int not null primary key, -- default when not run through sqlpp
--else
  --ifdef MYSQL or POSTGRES
  --#id    serial primary key, -- MySQL or Postgres
  --else
  --#id    integer primary key, -- SQLite version
  --endif
--endif
   label  varchar(100))
--ifdef MySQL
engine = InnoDB
--endif
;

$ sqlpp sqlpp_example.sql
create table test (
   id    int not null primary key, -- default when not run through sqlpp
   label  varchar(100))
;

$ sqlpp -D MYSQL sqlpp_example.sql
create table test (
   id    serial primary key, -- MySQL or Postgres
   label  varchar(100))
engine = InnoDB
;

$ sqlpp -D SQLITE sqlpp_example.sql
create table test (
   id    integer primary key, -- SQLite version
   label  varchar(100))
;

$ sqlpp -D POSTGRES sqlpp_example.sql
create table test (
   id    serial primary key, -- MySQL or Postgres
   label  varchar(100))
;
~~~
