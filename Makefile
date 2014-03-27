HEADERS=\
	src/mongo/db/pipeline/value_storage_data.h \
	src/mongo/bson/bsonobj_holder.h

all: $(HEADERS)

%.h: %.pps
	PYTHONPATH=/home/jcarey/Git/PortablePackedStruct/PortablePackedStruct/lib python $< > $@
