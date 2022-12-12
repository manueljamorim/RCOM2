# RCOM2
 FTP APP

Using library for regular expressions PCRE2
* Mac: ```brew install pcre2```
* Ubuntu: ```apt-get install libpcre3 libpcre3-dev```
* ArchLinux: ```pacman -Su pcre pcre2```
* CentOS: ```yum install pcre pcre-devel```

Compile in my Mac (2014):
```
gcc main.c -I/usr/local/include -L/usr/local/lib  -lpcre2-8 -o main
./main download ftp://[<user>:<password>@]<host>/<url-path> 
```

Test:
ftp://ftp.up.pt/pub/CPAN/README.html
ftp://ftp.up.pt/pub/kodi/timestamp.txt
ftp://ftp.up.pt/scientific/documents/graphics/latest/backgrounds/Plain/sl-background.1024.600.png
ftp://up202007485:12A34@ftp.up.pt/pinguim/hola/pinguim.gif (nt supposed to work)

Questions:
Modo passivo: o cliente liga-se ao servidor