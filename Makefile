TARGET := LAST

INC := -I/usr/include/lua5.* `pkg-config --cflags gtk+-3.0 x11 jansson`
CFLAGS := -std=gnu99 -O0 -pthread -Wall -Wno-unused-parameter
LDFLAGS := -llua `pkg-config --libs gtk+-3.0 x11 jansson`

SRC_DIR := ./src
OBJ_DIR := ./obj
ASSETS_DIR := ./assets

# Obtain list of source files and create list of object files
SOURCES := $(wildcard $(SRC_DIR)/*.c)
COMPONENTS := $(wildcard $(SRC_DIR)/components/*.c)
OBJECTS := $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SOURCES)) \
          $(patsubst $(SRC_DIR)/components/%.c, $(OBJ_DIR)/%.o, $(COMPONENTS))

BIN := last
BIN_DIR := /usr/local/bin
APP := last.desktop
APP_DIR := /usr/share/applications
ICON := last
ICON_DIR := /usr/share/icons/hicolor
SCHEMA := last.gschema.xml
SCHEMAS_DIR := /usr/share/glib-2.0/schemas

build: last-gtk.h $(TARGET)

# Rule to link object files to create executable
$(TARGET): $(OBJECTS)
	gcc $(CFLAGS) $^ $(LDFLAGS) -o $@

# Rule to compile C source files to object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	gcc $(INC) $(CFLAGS) -c -o $@ $<

# Rule to compile C component source files to object files
$(OBJ_DIR)/%.o: $(SRC_DIR)/components/%.c | $(OBJ_DIR)
	gcc $(INC) $(CFLAGS) -c -o $@ $<

# Rule to create the object directory
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

last-gtk.h: $(SRC_DIR)/last-gtk.css
	xxd --include $(SRC_DIR)/last-gtk.css > $(SRC_DIR)/last-gtk.h || (rm $(SRC_DIR)/last-gtk.h; false)

install:
	sudo cp $(TARGET) $(BIN_DIR)/$(BIN)
	sudo cp $(ASSETS_DIR)/$(APP) $(APP_DIR)
	for size in 16 22 24 32 36 48 64 72 96 128 256 512; do \
	  sudo convert assets/$(ICON).png -resize "$$size"x"$$size" \
	          $(ICON_DIR)/"$$size"x"$$size"/apps/$(ICON).png ; \
	done
	sudo gtk-update-icon-cache -f -t $(ICON_DIR)
	sudo cp $(SRC_DIR)/$(SCHEMA) $(SCHEMAS_DIR)
	sudo glib-compile-schemas $(SCHEMAS_DIR)

uninstall:
	sudo rm -f $(BIN_DIR)/$(BIN)
	sudo rm -f $(APP_DIR)/$(APP)
	for size in 16 22 24 32 36 48 64 72 96 128 256 512; do \
	  sudo rm -f $(ICON_DIR)/"$$size"x"$$size"/apps/$(ICON).png ; \
	done

remove-schema:
	sudo rm $(SCHEMAS_DIR)/$(SCHEMA)
	sudo glib-compile-schemas $(SCHEMAS_DIR)

# Clean target to remove object files and LAS executable
clean:
	rm -rf $(TARGET) $(OBJ_DIR) $(SRC_DIR)/last-gtk.h

.PHONY: build last-gtk.h install uninstall remove-schema clean
