
# Set to use Gnu debugger
GDB_FLAG = -g -O3
#GDB_FLAG = -g -O3 -ggdb

#CPPFLAGS += `pkg-config --cflags libvlc`
#LDFLAGS += `pkg-config --libs libvlc`

CFG_INC = -I./include -I/usr/include -I/usr/local/include -I/usr/local/include/cjson
LIB_PATH = -L/usr/local/lib -L./lib -L/usr/lib/x86_64-linux-gnu

# Initialize other variables
OUTDIR  = .
OBJDIR  = $(OUTDIR)
OUTFILE = $(OUTDIR)/test_sample

DEBUG_FLAG = -Wall -DLINUX # -DDEBUG

#LIB = -lpthread -lcjson -lginside -lKwsRnet -lvlc -lasound
LIB = -lpthread -lcjson -lginside -lKwsRnet -lasound

OBJ =	$(OBJDIR)/test_sample.o     \
	$(OBJDIR)/base64.o           
#	$(OBJDIR)/VLCPlayer.o

COMPILE = g++ -w -c $(GDB_FLAG) $(DEBUG_FLAG) -o "$(OUTDIR)/$(*F).o" $(CFG_INC) \
			"$<"
LINK = g++ $(GDB_FLAG) $(LDFLAGS) $(CPPFLAGS) -o "$(OUTFILE)" $(OBJ) $(CFG_LIB) $(LIB_PATH) $(LIB)

# Pattern rules

$(OUTDIR)/%.o : %.cpp
	$(COMPILE)

$(OUTFILE): $(OUTDIR) $(OBJ) $(INC)
	$(LINK)

# Create the release dir if doesn't exist
$(OUTDIR):
	mkdir -p "$(OUTDIR)"

# Build rules
all:
	$(MAKE) $(OUTFILE)

# Rebuild this project
rebuild: cleanall all

# Clean this project
clean:
	rm -f $(OUTFILE)
	rm -f $(OBJ)

# Clean this project and all dependencies
cleanall: clean
