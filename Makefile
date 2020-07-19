TARGET=vm
INSTALL_TARGET=/bin/rdram-vm

.PHONY: all install uninstall clean

all: $(TARGET)

$(TARGET): $(TARGET).cpp
	g++ $< -o $@

install: all
	cp -v vm $(INSTALL_TARGET)

uninstall:
	rm $(INSTALL_TARGET)

clean:
	rm -f $(TARGET)
