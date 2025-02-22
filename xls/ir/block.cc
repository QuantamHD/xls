// Copyright 2021 The XLS Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "xls/ir/block.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "xls/ir/function.h"
#include "xls/ir/node.h"
#include "xls/ir/node_iterator.h"

namespace xls {

std::string Block::DumpIr(bool recursive) const {
  // TODO(meheff): Remove recursive argument. Recursively dumping multiple
  // functions should be a method at the Package level, not the function/proc
  // level.
  XLS_CHECK(!recursive);

  std::vector<std::string> port_strings;
  for (const Port& port : GetPorts()) {
    if (absl::holds_alternative<ClockPort*>(port)) {
      port_strings.push_back(
          absl::StrFormat("%s: clock", absl::get<ClockPort*>(port)->name));
    } else if (absl::holds_alternative<InputPort*>(port)) {
      port_strings.push_back(
          absl::StrFormat("%s: %s", absl::get<InputPort*>(port)->GetName(),
                          absl::get<InputPort*>(port)->GetType()->ToString()));
    } else {
      port_strings.push_back(absl::StrFormat(
          "%s: %s", absl::get<OutputPort*>(port)->GetName(),
          absl::get<OutputPort*>(port)->operand(0)->GetType()->ToString()));
    }
  }
  std::string res = absl::StrFormat("block %s(%s) {\n", name(),
                                    absl::StrJoin(port_strings, ", "));

  for (Register* reg : GetRegisters()) {
    if (reg->reset().has_value()) {
      absl::StrAppendFormat(
          &res,
          "  reg %s(%s, reset_value=%s, asynchronous=%s, active_low=%s)\n",
          reg->name(), reg->type()->ToString(),
          reg->reset().value().reset_value.ToHumanString(),
          reg->reset().value().asynchronous ? "true" : "false",
          reg->reset().value().active_low ? "true" : "false");
    } else {
      absl::StrAppendFormat(&res, "  reg %s(%s)\n", reg->name(),
                            reg->type()->ToString());
    }
  }

  for (Node* node : TopoSort(const_cast<Block*>(this))) {
    absl::StrAppend(&res, "  ", node->ToString(), "\n");
  }
  absl::StrAppend(&res, "}\n");
  return res;
}

absl::Status Block::SetPortNameExactly(absl::string_view name, Node* node) {
  // TODO(https://github.com/google/xls/issues/477): If this name is an invalid
  // Verilog identifier then an error should be returned.
  XLS_RET_CHECK(node->Is<InputPort>() || node->Is<OutputPort>());

  if (node->GetName() == name) {
    return absl::OkStatus();
  }

  XLS_RET_CHECK(node->function_base() == this);
  for (Node* n : nodes()) {
    if (n->GetName() == name) {
      if (n->Is<InputPort>() || n->Is<OutputPort>()) {
        return absl::InvalidArgumentError(
            absl::StrFormat("Cannot name port `%s` because a port "
                            "already exists with this name",
                            name));
      }
      // Pick a new name for n.
      n->name_ = UniquifyNodeName(name);
      XLS_RET_CHECK_NE(n->GetName(), name);
      node->name_ = name;
      return absl::OkStatus();
    }
  }
  // Ensure the name is known by the uniquer.
  UniquifyNodeName(name);
  node->name_ = name;
  return absl::OkStatus();
}

absl::StatusOr<InputPort*> Block::AddInputPort(
    absl::string_view name, Type* type, absl::optional<SourceLocation> loc) {
  if (ports_by_name_.contains(name)) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Block %s already contains a port named %s", this->name(), name));
  }
  InputPort* port =
      AddNode(absl::make_unique<InputPort>(loc, name, type, this));
  if (name != port->GetName()) {
    // The name uniquer changed the given name of the input port to preserve
    // name uniqueness which means another node with this name may already
    // exist.  Force the `port` to have this name potentially be renaming the
    // colliding node.
    XLS_RETURN_IF_ERROR(SetPortNameExactly(name, port));
  }

  ports_by_name_[name] = port;
  ports_.push_back(port);
  input_ports_.push_back(port);
  return port;
}

absl::StatusOr<OutputPort*> Block::AddOutputPort(
    absl::string_view name, Node* operand, absl::optional<SourceLocation> loc) {
  if (ports_by_name_.contains(name)) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Block %s already contains a port named %s", this->name(), name));
  }
  OutputPort* port =
      AddNode(absl::make_unique<OutputPort>(loc, operand, name, this));

  if (name != port->GetName()) {
    // The name uniquer changed the given name of the output port to preserve
    // name uniqueness which means another node with this name may already
    // exist.  Force the `port` to have this name potentially be renaming the
    // colliding node.
    XLS_RETURN_IF_ERROR(SetPortNameExactly(name, port));
  }
  ports_by_name_[name] = port;
  ports_.push_back(port);
  output_ports_.push_back(port);
  return port;
}

absl::StatusOr<Register*> Block::AddRegister(absl::string_view name, Type* type,
                                             absl::optional<Reset> reset) {
  if (registers_.contains(name)) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Register already exists with name %s", name));
  }
  if (reset.has_value()) {
    if (type != package()->GetTypeForValue(reset.value().reset_value)) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "Reset value %s for register %s is not of type %s",
          reset.value().reset_value.ToString(), name, type->ToString()));
    }
  }
  registers_[name] =
      absl::make_unique<Register>(std::string(name), type, reset);
  register_vec_.push_back(registers_[name].get());
  Register* reg = register_vec_.back();
  register_reads_[reg] = {};
  register_writes_[reg] = {};

  return register_vec_.back();
}

absl::Status Block::RemoveRegister(Register* reg) {
  if (!IsOwned(reg)) {
    return absl::InvalidArgumentError("Register is not owned by block.");
  }

  if (!register_reads_.at(reg).empty() || !register_writes_.at(reg).empty()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Register %s can't be removed because a register read "
                        "or write operation for this register still exists",
                        reg->name()));
  }
  register_reads_.erase(reg);
  register_writes_.erase(reg);

  auto it = std::find(register_vec_.begin(), register_vec_.end(), reg);
  XLS_RET_CHECK(it != register_vec_.end());
  register_vec_.erase(it);
  registers_.erase(reg->name());
  return absl::OkStatus();
}

absl::StatusOr<Register*> Block::GetRegister(absl::string_view name) const {
  if (!registers_.contains(name)) {
    return absl::NotFoundError(absl::StrFormat(
        "Block %s has no register named %s", this->name(), name));
  }
  return registers_.at(name).get();
}

absl::Status Block::AddClockPort(absl::string_view name) {
  if (clock_port_.has_value()) {
    return absl::InternalError("Block already has clock");
  }
  if (ports_by_name_.contains(name)) {
    return absl::InternalError(
        absl::StrFormat("Block already has a port named %s", name));
  }
  clock_port_ = ClockPort{std::string(name)};
  ports_.push_back(&clock_port_.value());
  return absl::OkStatus();
}

// Removes the element `node` from the vector element in the given map at the
// given key. Used for updated register_read_ and register_write_ data members
// of Block.
template <typename KeyT, typename NodeT>
static absl::Status RemoveFromMapOfNodeVectors(
    KeyT key, NodeT* node,
    absl::flat_hash_map<KeyT, std::vector<NodeT*>>* map) {
  XLS_RET_CHECK(map->contains(key)) << node->GetName();
  std::vector<NodeT*>& vector = map->at(key);
  auto it = std::find(vector.begin(), vector.end(), node);
  XLS_RET_CHECK(it != vector.end()) << node->GetName();
  vector.erase(it);
  return absl::OkStatus();
}

// Adds the element `node` to the vector element in the given map at the given
// key. Used for updated register_read_ and register_write_ data members of
// Block.
template <typename KeyT, typename NodeT>
static absl::Status AddToMapOfNodeVectors(
    KeyT key, NodeT* node,
    absl::flat_hash_map<KeyT, std::vector<NodeT*>>* map) {
  XLS_RET_CHECK(map->contains(key)) << node->GetName();
  map->at(key).push_back(node);
  return absl::OkStatus();
}

Node* Block::AddNodeInternal(std::unique_ptr<Node> node) {
  Node* ptr = FunctionBase::AddNodeInternal(std::move(node));
  if (RegisterRead* reg_read = dynamic_cast<RegisterRead*>(ptr)) {
    XLS_CHECK_OK(AddToMapOfNodeVectors(reg_read->GetRegister(), reg_read,
                                       &register_reads_));
  } else if (RegisterWrite* reg_write = dynamic_cast<RegisterWrite*>(ptr)) {
    XLS_CHECK_OK(AddToMapOfNodeVectors(reg_write->GetRegister(), reg_write,
                                       &register_writes_));
  }
  return ptr;
}

absl::Status Block::RemoveNode(Node* n) {
  // Simliar to parameters in xls::Functions, input and output ports are also
  // also stored separately as vectors for easy access and to indicate ordering.
  // Fix up these vectors prior to removing the node.
  if (n->Is<InputPort>() || n->Is<OutputPort>()) {
    Port port;
    if (n->Is<InputPort>()) {
      port = n->As<InputPort>();
      auto it = std::find(input_ports_.begin(), input_ports_.end(),
                          n->As<InputPort>());
      XLS_RET_CHECK(it != input_ports_.end()) << absl::StrFormat(
          "input port node %s is not in the vector of input ports",
          n->GetName());
      input_ports_.erase(it);
    } else if (n->Is<OutputPort>()) {
      port = n->As<OutputPort>();
      auto it = std::find(output_ports_.begin(), output_ports_.end(),
                          n->As<OutputPort>());
      XLS_RET_CHECK(it != output_ports_.end()) << absl::StrFormat(
          "output port node %s is not in the vector of output ports",
          n->GetName());
      output_ports_.erase(it);
    }
    ports_by_name_.erase(n->GetName());
    auto port_it = std::find(ports_.begin(), ports_.end(), port);
    XLS_RET_CHECK(port_it != ports_.end()) << absl::StrFormat(
        "port node %s is not in the vector of ports", n->GetName());
    ports_.erase(port_it);
  } else if (RegisterRead* reg_read = dynamic_cast<RegisterRead*>(n)) {
    XLS_RETURN_IF_ERROR(RemoveFromMapOfNodeVectors(reg_read->GetRegister(),
                                                   reg_read, &register_reads_));
  } else if (RegisterWrite* reg_write = dynamic_cast<RegisterWrite*>(n)) {
    XLS_RETURN_IF_ERROR(RemoveFromMapOfNodeVectors(
        reg_write->GetRegister(), reg_write, &register_writes_));
  }

  return FunctionBase::RemoveNode(n);
}

absl::StatusOr<RegisterRead*> Block::GetRegisterRead(Register* reg) const {
  XLS_RET_CHECK(register_reads_.contains(reg)) << absl::StreamFormat(
      "Block %s does not have register %s (%p)", name(), reg->name(), reg);
  const std::vector<RegisterRead*>& reads = register_reads_.at(reg);
  if (reads.size() == 1) {
    return reads.front();
  }
  if (reads.empty()) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "Block %s has no read operation for register %s", name(), reg->name()));
  }
  return absl::InvalidArgumentError(
      absl::StrFormat("Block %s has multiple read operation for register %s",
                      name(), reg->name()));
}

absl::StatusOr<RegisterWrite*> Block::GetRegisterWrite(Register* reg) const {
  XLS_RET_CHECK(register_writes_.contains(reg)) << absl::StreamFormat(
      "Block %s does not have register %s (%p)", name(), reg->name(), reg);
  const std::vector<RegisterWrite*>& writes = register_writes_.at(reg);
  if (writes.size() == 1) {
    return writes.front();
  }
  if (writes.empty()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("Block %s has no write operation for register %s",
                        name(), reg->name()));
  }
  return absl::InvalidArgumentError(
      absl::StrFormat("Block %s has multiple write operation for register %s",
                      name(), reg->name()));
}

absl::Status Block::ReorderPorts(absl::Span<const std::string> port_names) {
  absl::flat_hash_map<std::string, int64_t> port_order;
  for (int64_t i = 0; i < port_names.size(); ++i) {
    port_order[port_names[i]] = i;
  }
  XLS_RET_CHECK_EQ(port_order.size(), port_names.size())
      << "Port order has duplicate names";
  for (const Port& port : GetPorts()) {
    XLS_RET_CHECK(port_order.contains(PortName(port)))
        << absl::StreamFormat("Port order missing port \"%s\"", PortName(port));
  }
  XLS_RET_CHECK_EQ(port_order.size(), GetPorts().size())
      << "Port order includes invalid port names";
  std::sort(ports_.begin(), ports_.end(), [&](const Port& a, const Port& b) {
    return port_order.at(PortName(a)) < port_order.at(PortName(b));
  });
  return absl::OkStatus();
}

/*static*/ std::string Block::PortName(const Port& port) {
  if (absl::holds_alternative<ClockPort*>(port)) {
    return absl::get<ClockPort*>(port)->name;
  } else if (absl::holds_alternative<InputPort*>(port)) {
    return absl::get<InputPort*>(port)->GetName();
  } else {
    return absl::get<OutputPort*>(port)->GetName();
  }
}

}  // namespace xls
