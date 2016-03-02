/*
 * nghttp2 - HTTP/2 C Library
 *
 * Copyright (c) 2015 Tatsuhiro Tsujikawa
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include "shrpx_mruby_module_env.h"

#include <mruby/variable.h>
#include <mruby/string.h>
#include <mruby/hash.h>

#include "shrpx_downstream.h"
#include "shrpx_upstream.h"
#include "shrpx_client_handler.h"
#include "shrpx_mruby.h"
#include "shrpx_mruby_module.h"

namespace shrpx {

namespace mruby {

namespace {
mrb_value env_init(mrb_state *mrb, mrb_value self) { return self; }
} // namespace

namespace {
mrb_value env_get_req(mrb_state *mrb, mrb_value self) {
  return mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "req"));
}
} // namespace

namespace {
mrb_value env_get_resp(mrb_state *mrb, mrb_value self) {
  return mrb_iv_get(mrb, self, mrb_intern_lit(mrb, "resp"));
}
} // namespace

namespace {
mrb_value env_get_ctx(mrb_state *mrb, mrb_value self) {
  auto data = reinterpret_cast<MRubyAssocData *>(mrb->ud);
  auto downstream = data->downstream;

  auto dsym = intern_ptr(mrb, downstream);

  auto ctx = mrb_iv_get(mrb, self, dsym);
  if (mrb_nil_p(ctx)) {
    ctx = mrb_hash_new(mrb);
    mrb_iv_set(mrb, self, dsym, ctx);
  }

  return ctx;
}
} // namespace

namespace {
mrb_value env_get_phase(mrb_state *mrb, mrb_value self) {
  auto data = static_cast<MRubyAssocData *>(mrb->ud);

  return mrb_fixnum_value(data->phase);
}
} // namespace

namespace {
mrb_value env_get_remote_addr(mrb_state *mrb, mrb_value self) {
  auto data = static_cast<MRubyAssocData *>(mrb->ud);
  auto downstream = data->downstream;
  auto upstream = downstream->get_upstream();
  auto handler = upstream->get_client_handler();

  auto &ipaddr = handler->get_ipaddr();

  return mrb_str_new(mrb, ipaddr.c_str(), ipaddr.size());
}
} // namespace

void init_env_class(mrb_state *mrb, RClass *module) {
  auto env_class =
      mrb_define_class_under(mrb, module, "Env", mrb->object_class);

  mrb_define_method(mrb, env_class, "initialize", env_init, MRB_ARGS_NONE());
  mrb_define_method(mrb, env_class, "req", env_get_req, MRB_ARGS_NONE());
  mrb_define_method(mrb, env_class, "resp", env_get_resp, MRB_ARGS_NONE());
  mrb_define_method(mrb, env_class, "ctx", env_get_ctx, MRB_ARGS_NONE());
  mrb_define_method(mrb, env_class, "phase", env_get_phase, MRB_ARGS_NONE());
  mrb_define_method(mrb, env_class, "remote_addr", env_get_remote_addr,
                    MRB_ARGS_NONE());
}

} // namespace mruby

} // namespace shrpx
