/*
 * Copyright 2026 Aethernet Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef AETHER_OBJECTS_TESTS_TEST_OBJECT_SYSTEM_OBJECTS_ANCESTOR_LAYERS_H_
#define AETHER_OBJECTS_TESTS_TEST_OBJECT_SYSTEM_OBJECTS_ANCESTOR_LAYERS_H_

#include "aether-objects/obj/obj.h"

namespace ae {

struct AncProbe {
  int layer{0};
};

class AncPeer : public Obj {
  AE_OBJECT(AncPeer, Obj, 0)

 protected:
  AncPeer() = default;

 public:
  explicit AncPeer(ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(back))

  ObjPtr<Obj> back;
};

class AncBase : public Obj {
  AE_OBJECT(AncBase, Obj, 0)

 protected:
  AncBase() = default;

 public:
  explicit AncBase(ObjProp prop) : Obj{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(base_value), AE_MMBR(mark), AE_MMBR(peer))

  int base_value{0};
  AncProbe mark{1};
  AncPeer::ptr peer;
};

class AncMiddle : public AncBase {
  AE_OBJECT(AncMiddle, AncBase, 0)

 protected:
  AncMiddle() = default;

 public:
  explicit AncMiddle(ObjProp prop) : AncBase{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(middle_value), AE_MMBR(mark))

  int middle_value{0};
  AncProbe mark{2};
};

class AncStored : public AncMiddle {
  AE_OBJECT(AncStored, AncMiddle, 0)

 protected:
  AncStored() = default;

 public:
  explicit AncStored(ObjProp prop) : AncMiddle{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(stored_value), AE_MMBR(mark))

  int stored_value{0};
  AncProbe mark{3};
};

class AncRuntime : public AncStored {
  AE_OBJECT(AncRuntime, AncStored, 0)

 protected:
  AncRuntime() = default;

 public:
  explicit AncRuntime(ObjProp prop) : AncStored{prop} {}

  AE_OBJECT_REFLECT(AE_MMBR(runtime_only))

  int runtime_only{7};
};

}  // namespace ae

#endif  // AETHER_OBJECTS_TESTS_TEST_OBJECT_SYSTEM_OBJECTS_ANCESTOR_LAYERS_H_
