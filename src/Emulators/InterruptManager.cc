#include "InterruptManager.hh"

using namespace std;

InterruptManager::InterruptManager() : cycle_count(0) {}

shared_ptr<InterruptManager::PendingCall> InterruptManager::add(
    uint64_t after_cycles, function<bool()> fn) {
  shared_ptr<PendingCall> ret(new PendingCall());
  ret->at_cycle_count = this->cycle_count + after_cycles;
  ret->canceled = false;
  ret->completed = false;
  ret->fn = move(fn);

  if (!this->head.get()) {
    this->head = ret;
  } else {
    if (ret->at_cycle_count < this->head->at_cycle_count) {
      ret->next = this->head;
      this->head = ret;
    } else {
      shared_ptr<PendingCall> prev = this->head;
      shared_ptr<PendingCall> next = this->head->next;
      while (next.get() && next->at_cycle_count < ret->at_cycle_count) {
        prev = next;
        next = next->next;
      }
      ret->next = next;
      prev->next = ret;
    }
  }

  return ret;
}

void InterruptManager::on_cycle_start() {
  this->cycle_count++;

  while (this->head.get() && (this->head->at_cycle_count <= this->cycle_count)) {
    shared_ptr<PendingCall> c = this->head;
    this->head = c->next;
    if (!c->canceled) {
      c->fn();
    }
    c->completed = true;
  }
}

uint64_t InterruptManager::cycles() const {
  return this->cycle_count;
}
