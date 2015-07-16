Cmux
====
This program enables GSM 0710 multiplexing using linux n_gsm line dicipline.
Supported are also SIM900 and Telit modules.

How it works
-------
This program :
* Opens modem device on serial line
* Enables modem CMUX
* Attaches n_gsm line dicipline
* Creates virtual TTYs
* Daemonizes and waits for signal
* Removes the virtual TTYs

In order to activate the CMUX mode the program sends some AT commands.
Theses commands are mainly vendor specific and should be adapted to your own modem.
The example in this code works for the Quectel M95 GSM module.

How to
------
* In order to run this program you should have `n_gsm` module built for your linux kernel and loaded.
  Use `modprobe n_gsm` to load it. If it fails you probably have to build the kernel module by yourself.
* Change the defined options in `cmux.c`:

```
$ ./cmux -h
Usage: cmux --device /dev/ttyUSB0 --speed 115200

--type <type>	SIM900, TELIT or default. (Default: default)
--device <name>	Serial device name. (Default: /dev/ttyUSB0)
--speed <rate>	Serial device line speed. (Default: 115200)
--mtu <number>	MTU size. (Default: 512)
--debug [1|0]	Enable debugging. (Default: 1)
--daemon [1|0]	Fork into background. (Default: 1)
--driver <name>	Driver to use. (Default: gsmtty)
--base <name>	Base name for the nodes. (Default: /dev/ttyGSM)
--nodes [0-4]	Number of nodes to create. (Default: 1)
```

* Change the AT commands set to fit your modem in `cmux.c`.
* Call make and run the program.

```
make
./cmux
```

It will create up to four devices:
* /dev/ttyGSM1
* /dev/ttyGSM2
* /dev/ttyGSM3
* /dev/ttyGSM4

You should now be able to access your modem with four TTYs and both use `ppp` and send AT commands to your modem.

Links
-----
This program is mainly an actual implementation of [GSM 0710 tty multiplexor HOWTO](http://stuff.mit.edu/afs/sipb/contrib/linux/Documentation/serial/n_gsm.txt)

License
-----
CC0
