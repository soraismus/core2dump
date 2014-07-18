#include "visitor.h"
#include "common.h"
#include "error.h"
#include "queue.h"
#include "state.h"
#include "v8helpers.h"
#include "v8constants.h"

#include <stdlib.h>

static cd_error_t cd_visit_root(cd_state_t* state, cd_node_t* node);
static cd_error_t cd_queue_ptr(cd_state_t* state, char* ptr);
static cd_error_t cd_queue_space(cd_state_t* state, char* start, char* end);
static cd_error_t cd_add_node(cd_state_t* state,
                              cd_node_t* node,
                              void* map,
                              int type);


cd_error_t cd_visitor_init(cd_state_t* state) {
  QUEUE_INIT(&state->nodes);
  state->node_count = 0;

  return cd_ok();
}


void cd_visitor_destroy(cd_state_t* state) {
  while (!QUEUE_EMPTY(&state->nodes)) {
    QUEUE* q;
    cd_node_t* node;

    q = QUEUE_HEAD(&state->nodes);
    QUEUE_REMOVE(q);

    node = container_of(q, cd_node_t, member);
    free(node);
  }
}


cd_error_t cd_visit_roots(cd_state_t* state) {
  while (!QUEUE_EMPTY(&state->queue) != 0) {
    QUEUE* q;
    cd_node_t* node;

    q = QUEUE_HEAD(&state->queue);
    QUEUE_REMOVE(q);
    node = container_of(q, cd_node_t, member);

    /* Node will be readded to `nodes` in case of success */
    if (!cd_is_ok(cd_visit_root(state, node)))
      free(node);
  }

  return cd_ok();
}


#define T(A, B) CD_V8_TYPE(A, B)


cd_error_t cd_visit_root(cd_state_t* state, cd_node_t* node) {
  void** pmap;
  void* map;
  char* start;
  char* end;
  uint8_t* ptype;
  int type;
  cd_error_t err;

  V8_CORE_PTR(node->obj, cd_v8_class_HeapObject__map__Map, pmap);

  /* If zapped - the node was already added */
  map = *pmap;
  if (((intptr_t) map & state->zap_bit) == state->zap_bit)
    return cd_error(kCDErrAlreadyVisited);
  *pmap = (void*) ((intptr_t) map | state->zap_bit);

  /* Enqueue map itself */
  err = cd_queue_ptr(state, map);
  if (!cd_is_ok(err))
    return err;

  /* Load object type */
  V8_CORE_PTR(map, cd_v8_class_Map__instance_attributes__int, ptype);
  type = *ptype;

  /* Mimique the v8's behaviour, see HeapObject::IterateBody */

  /* Strings... ignore for now */
  if (type < cd_v8_FirstNonstringType)
    goto done;

  start = NULL;
  end = NULL;

  if (type == T(JSObject, JS_OBJECT) ||
      type == T(JSValue, JS_VALUE) ||
      type == T(JSDate, JS_DATE) ||
      type == T(JSArray, JS_ARRAY) ||
      type == T(JSArrayBuffer, JS_ARRAY_BUFFER) ||
      type == T(JSTypedArray, JS_TYPED_ARRAY) ||
      type == T(JSDataView, JS_DATA_VIEW) ||
      type == T(JSRegExp, JS_REGEXP) ||
      type == T(JSGlobalObject, JS_GLOBAL_OBJECT) ||
      type == T(JSBuiltinsObject, JS_BUILTINS_OBJECT) ||
      type == T(JSMessageObject, JS_MESSAGE_OBJECT) ||
      /* NOTE: Function has non-heap fields, but who cares! */
      type == T(JSFunction, JS_FUNCTION)) {
    /* General object */
    int size;
    int off;

    err = cd_v8_get_obj_size(state, map, type, &size);
    if (!cd_is_ok(err))
      return err;

    off = cd_v8_class_JSObject__properties__FixedArray;
    V8_CORE_PTR(node->obj, off, start);
    V8_CORE_PTR(node->obj, off + size, end);
  } else if (type == T(Map, MAP)) {
    int off;

    /* XXX Map::kPrototypeOffset = Map::kInstanceAttributes + kIntSize */
    off = cd_v8_class_Map__instance_attributes__int + 4;
    V8_CORE_PTR(node->obj, off, start);

    /* Constructor + Prototype */
    V8_CORE_PTR(node->obj, off + state->ptr_size * 2, end);
  } else {
    /* Unknown type - ignore */
    goto done;
  }

  if (start != NULL && end != NULL)
    cd_queue_space(state, start, end);

done:
  return cd_add_node(state, node, map, type);
}


cd_error_t cd_queue_ptr(cd_state_t* state, char* ptr) {
  cd_node_t* node;

  if (!V8_IS_HEAPOBJECT(ptr))
    return cd_error(kCDErrNotObject);

  node = malloc(sizeof(*node));
  if (node == NULL)
    return cd_error_str(kCDErrNoMem, "queue ptr");

  node->obj = ptr;
  QUEUE_INSERT_TAIL(&state->queue, &node->member);

  return cd_ok();
}


cd_error_t cd_queue_space(cd_state_t* state, char* start, char* end) {
  size_t delta;

  delta = cd_obj_is_x64(state->core) ? 8 : 4;
  for (; start < end; start += delta) {
    cd_error_t err;

    err = cd_queue_ptr(state, *(void**) start);
    if (!cd_is_ok(err))
      return err;
  }

  return cd_ok();
}


cd_error_t cd_add_node(cd_state_t* state,
                       cd_node_t* node,
                       void* map,
                       int type) {
  cd_error_t err;
  const char* cname;

  /* Mimique V8HeapExplorer::AddEntry */
  if (type == T(JSFunction, JS_FUNCTION)) {
    void** ptr;
    void* sh;
    void* name;

    /* Load shared function info to lookup name */
    V8_CORE_PTR(node->obj,
                cd_v8_class_JSFunction__shared__SharedFunctionInfo,
                ptr);
    sh = *ptr;

    V8_CORE_PTR(sh, cd_v8_class_SharedFunctionInfo__name__Object, ptr);
    name = *ptr;

    err = cd_v8_to_cstr(state, name, &cname, &node->name);
    if (!cd_is_ok(err))
      return err;

    node->type = kCDNodeClosure;
  } else {
    err = cd_strings_copy(&state->strings, &cname, &node->name, "", 0);
    if (!cd_is_ok(err))
      return err;

    node->type = kCDNodeHidden;
  }

  err = cd_v8_get_obj_size(state, map, type, &node->size);
  if (!cd_is_ok(err))
    return err;

  node->id = state->node_count++;

  QUEUE_INSERT_TAIL(&state->nodes, &node->member);

  return cd_ok();
}


#undef T
