Cmux
====
This program enables GSM 0710 multiplexing using linux n_gsm line dicipline.

How it works
-------
This program :
* Opens modem device on serial line
* Enables modem CMUX
* Attaches n_gsm line dicipline
* Creates virtual TTYs
* Daemonizes and waits for signal
* Removes the virtual TTYs

In order to activate the CMUX mode the program sends some AT commands. Theses commands are mainly vendor specific and should be adapted to your own modem. The example in this code works for the Quectel M95 GSM module.

How to
------
* In order to run this program you should have n_gsm module built for your linux kernel.
	modprobe n_gsm

* Change the defined options
* Change the AT commands set to fit your modem
* Make and run the program

You should now be able to access your modem with four TTYs and use both ppp and AT commands as you want.

Links
-----
This program is mainly an actual implementation of [GSM 0710 tty multiplexor HOWTO](http://stuff.mit.edu/afs/sipb/contrib/linux/Documentation/serial/n_gsm.txt)
