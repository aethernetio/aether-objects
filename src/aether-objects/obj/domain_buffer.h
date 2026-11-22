#ifndef AETHER_OBJECTS_OBJ_DOMAIN_BUFFER_H_
#define AETHER_OBJECTS_OBJ_DOMAIN_BUFFER_H_

#include "aether-miscpp/serialization/binary_archive.h"
#include "aether-miscpp/serialization/serialization.h"

#include "aether-objects/obj/idomain_storage.h"
#include "aether-objects/obj/obj_id.h"

namespace ae {

class DomainGraph;

struct DomainBuffer {
  seri::SeriResult Write(seri::SizeWriteTag data) const {
    if (writer == nullptr) {
      return Error{seri::write_error};
    }
    return writer->Write(data);
  }
  seri::SeriResult Write(seri::DataWriteTag data) const {
    if (writer == nullptr) {
      return Error{seri::write_error};
    }
    return writer->Write(data);
  }

  seri::SeriResult Read(seri::SizeReadTag data) const {
    if (reader == nullptr) {
      return Error{seri::read_error};
    }
    return reader->Read(data);
  }
  seri::SeriResult Read(seri::DataReadTag data) const {
    if (reader == nullptr) {
      return Error{seri::read_error};
    }
    return reader->Read(data);
  }

  ObjId id;
  DomainGraph* domain_graph{};
  IDomainStorageWriter* writer{nullptr};
  IDomainStorageReader* reader{nullptr};
};

}  // namespace ae

#endif  // AETHER_OBJECTS_OBJ_DOMAIN_BUFFER_H_
