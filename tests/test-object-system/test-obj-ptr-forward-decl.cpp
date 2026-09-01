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

#include <type_traits>
#include <utility>

#include "aether-objects/obj/obj_ptr.h"

namespace ae {
class ForwardDeclaredObj;

namespace test_obj_ptr_forward_decl_internal {

class Base {};
class Derived : public Base {};
class Unrelated {};

static_assert(AbleToCast<Base, Derived>);
static_assert(AbleToCast<Derived, Base>);
static_assert(!AbleToCast<Base, Unrelated>);
static_assert(!AbleToCast<Unrelated, Base>);

static_assert(std::is_constructible_v<ObjPtr<Base>, ObjPtr<Derived>>);
static_assert(std::is_constructible_v<ObjPtr<Derived>, ObjPtr<Base>>);
static_assert(!std::is_constructible_v<ObjPtr<Base>, ObjPtr<Unrelated>>);
static_assert(!std::is_constructible_v<ObjPtr<Unrelated>, ObjPtr<Base>>);

}  // namespace test_obj_ptr_forward_decl_internal

void test_ObjPtrForwardDeclarationCompile() {
  ObjPtr<ForwardDeclaredObj> empty;
  auto copy = empty;
  auto moved = std::move(copy);
  empty = std::move(moved);
}
}  // namespace ae
