all:
	$(MAKE) -C lib
	$(MAKE) -C init
	$(MAKE) -C users

clean:
	$(MAKE) -C lib   clean
	$(MAKE) -C init  clean
	$(MAKE) -C users clean
