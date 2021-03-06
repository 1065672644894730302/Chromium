// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/download/byte_stream.h"

#include "base/bind.h"
#include "base/location.h"
#include "base/memory/weak_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/sequenced_task_runner.h"

namespace {

typedef std::deque<std::pair<scoped_refptr<net::IOBuffer>, size_t> >
ContentVector;

// The fraction of the buffer that must be ready to send on the input
// before we ship data to the output.
static const int kFractionBufferBeforeSending = 3;

// The fraction of the buffer that must have been consumed on the output
// before we update the input window.
static const int kFractionReadBeforeWindowUpdate = 3;

class ByteStreamOutputImpl;

// A poor man's weak pointer; a RefCountedThreadSafe boolean that can be
// cleared in an object destructor and accessed to check for object
// existence.  We can't use weak pointers because they're tightly tied to
// threads rather than task runners.
// TODO(rdsmith): A better solution would be extending weak pointers
// to support SequencedTaskRunners.
struct LifetimeFlag : public base::RefCountedThreadSafe<LifetimeFlag> {
 public:
  LifetimeFlag() : is_alive_(true) { }
  bool is_alive_;

 protected:
  friend class base::RefCountedThreadSafe<LifetimeFlag>;
  virtual ~LifetimeFlag() { }

 private:
  DISALLOW_COPY_AND_ASSIGN(LifetimeFlag);
};

// For both ByteStreamInputImpl and ByteStreamOutputImpl, Construction and
// SetPeer may happen anywhere; all other operations on each class must
// happen in the context of their SequencedTaskRunner.
class ByteStreamInputImpl : public content::ByteStreamInput {
 public:
  ByteStreamInputImpl(scoped_refptr<base::SequencedTaskRunner> task_runner,
                      scoped_refptr<LifetimeFlag> lifetime_flag,
                      size_t buffer_size);
  virtual ~ByteStreamInputImpl();

  // Must be called before any operations are performed.
  void SetPeer(ByteStreamOutputImpl* peer,
               scoped_refptr<base::SequencedTaskRunner> peer_task_runner,
               scoped_refptr<LifetimeFlag> peer_lifetime_flag);

  // Overridden from ByteStreamInput.
  virtual bool Write(scoped_refptr<net::IOBuffer> buffer,
                     size_t byte_count) OVERRIDE;
  virtual void Close(content::DownloadInterruptReason status) OVERRIDE;
  virtual void RegisterCallback(const base::Closure& source_callback) OVERRIDE;

  // PostTask target from |ByteStreamOutputImpl::MaybeUpdateInput|.
  static void UpdateWindow(scoped_refptr<LifetimeFlag> lifetime_flag,
                           ByteStreamInputImpl* target,
                           size_t bytes_consumed);

 private:
  // Called from UpdateWindow when object existence has been validated.
  void UpdateWindowInternal(size_t bytes_consumed);

  void PostToPeer(bool complete, content::DownloadInterruptReason status);

  const size_t total_buffer_size_;

  // All data objects in this class are only valid to access on
  // this task runner except as otherwise noted.
  scoped_refptr<base::SequencedTaskRunner> my_task_runner_;

  // True while this object is alive.
  scoped_refptr<LifetimeFlag> my_lifetime_flag_;

  base::Closure space_available_callback_;
  ContentVector input_contents_;
  size_t input_contents_size_;

  // ** Peer information.

  scoped_refptr<base::SequencedTaskRunner> peer_task_runner_;

  // How much we've sent to the output that for flow control purposes we
  // must assume hasn't been read yet.
  size_t output_size_used_;

  // Only valid to access on peer_task_runner_.
  scoped_refptr<LifetimeFlag> peer_lifetime_flag_;

  // Only valid to access on peer_task_runner_ if
  // |*peer_lifetime_flag_ == true|
  ByteStreamOutputImpl* peer_;
};

class ByteStreamOutputImpl : public content::ByteStreamOutput {
 public:
  ByteStreamOutputImpl(scoped_refptr<base::SequencedTaskRunner> task_runner,
                       scoped_refptr<LifetimeFlag> lifetime_flag,
                       size_t buffer_size);
  virtual ~ByteStreamOutputImpl();

  // Must be called before any operations are performed.
  void SetPeer(ByteStreamInputImpl* peer,
               scoped_refptr<base::SequencedTaskRunner> peer_task_runner,
               scoped_refptr<LifetimeFlag> peer_lifetime_flag);

  // Overridden from ByteStreamOutput.
  virtual StreamState Read(scoped_refptr<net::IOBuffer>* data,
                           size_t* length) OVERRIDE;
  virtual content::DownloadInterruptReason GetStatus() const OVERRIDE;
  virtual void RegisterCallback(const base::Closure& sink_callback) OVERRIDE;

  // PostTask target from |ByteStreamInputImpl::MaybePostToPeer| and
  // |ByteStreamInputImpl::Close|.
  // Receive data from our peer.
  // static because it may be called after the object it is targeting
  // has been destroyed.  It may not access |*target|
  // if |*object_lifetime_flag| is false.
  static void TransferData(
      scoped_refptr<LifetimeFlag> object_lifetime_flag,
      ByteStreamOutputImpl* target,
      scoped_ptr<ContentVector> transfer_buffer,
      size_t transfer_buffer_bytes,
      bool source_complete,
      content::DownloadInterruptReason status);

 private:
  // Called from TransferData once object existence has been validated.
  void TransferDataInternal(
      scoped_ptr<ContentVector> transfer_buffer,
      size_t transfer_buffer_bytes,
      bool source_complete,
      content::DownloadInterruptReason status);

  void MaybeUpdateInput();

  const size_t total_buffer_size_;

  scoped_refptr<base::SequencedTaskRunner> my_task_runner_;

  // True while this object is alive.
  scoped_refptr<LifetimeFlag> my_lifetime_flag_;

  ContentVector available_contents_;

  bool received_status_;
  content::DownloadInterruptReason status_;

  base::Closure data_available_callback_;

  // Time of last point at which data in stream transitioned from full
  // to non-full.  Nulled when a callback is sent.
  base::Time last_non_full_time_;

  // ** Peer information

  scoped_refptr<base::SequencedTaskRunner> peer_task_runner_;

  // How much has been removed from this class that we haven't told
  // the input about yet.
  size_t unreported_consumed_bytes_;

  // Only valid to access on peer_task_runner_.
  scoped_refptr<LifetimeFlag> peer_lifetime_flag_;

  // Only valid to access on peer_task_runner_ if
  // |*peer_lifetime_flag_ == true|
  ByteStreamInputImpl* peer_;
};

ByteStreamInputImpl::ByteStreamInputImpl(
    scoped_refptr<base::SequencedTaskRunner> task_runner,
    scoped_refptr<LifetimeFlag> lifetime_flag,
    size_t buffer_size)
    : total_buffer_size_(buffer_size),
      my_task_runner_(task_runner),
      my_lifetime_flag_(lifetime_flag),
      input_contents_size_(0),
      output_size_used_(0),
      peer_(NULL) {
  DCHECK(my_lifetime_flag_.get());
  my_lifetime_flag_->is_alive_ = true;
}

ByteStreamInputImpl::~ByteStreamInputImpl() {
  my_lifetime_flag_->is_alive_ = false;
}

void ByteStreamInputImpl::SetPeer(
    ByteStreamOutputImpl* peer,
    scoped_refptr<base::SequencedTaskRunner> peer_task_runner,
    scoped_refptr<LifetimeFlag> peer_lifetime_flag) {
  peer_ = peer;
  peer_task_runner_ = peer_task_runner;
  peer_lifetime_flag_ = peer_lifetime_flag;
}

bool ByteStreamInputImpl::Write(
    scoped_refptr<net::IOBuffer> buffer, size_t byte_count) {
  DCHECK(my_task_runner_->RunsTasksOnCurrentThread());

  input_contents_.push_back(std::make_pair(buffer, byte_count));
  input_contents_size_ += byte_count;

  // Arbitrarily, we buffer to a third of the total size before sending.
  if (input_contents_size_ > total_buffer_size_ / kFractionBufferBeforeSending)
    PostToPeer(false, content::DOWNLOAD_INTERRUPT_REASON_NONE);

  return (input_contents_size_ + output_size_used_ <= total_buffer_size_);
}

void ByteStreamInputImpl::Close(
    content::DownloadInterruptReason status) {
  DCHECK(my_task_runner_->RunsTasksOnCurrentThread());
  PostToPeer(true, status);
}

void ByteStreamInputImpl::RegisterCallback(
    const base::Closure& source_callback) {
  DCHECK(my_task_runner_->RunsTasksOnCurrentThread());
  space_available_callback_ = source_callback;
}

// static
void ByteStreamInputImpl::UpdateWindow(
    scoped_refptr<LifetimeFlag> lifetime_flag, ByteStreamInputImpl* target,
    size_t bytes_consumed) {
  // If the target object isn't alive anymore, we do nothing.
  if (!lifetime_flag->is_alive_) return;

  target->UpdateWindowInternal(bytes_consumed);
}

void ByteStreamInputImpl::UpdateWindowInternal(size_t bytes_consumed) {
  DCHECK(my_task_runner_->RunsTasksOnCurrentThread());
  DCHECK_GE(output_size_used_, bytes_consumed);
  output_size_used_ -= bytes_consumed;

  // Callback if we were above the limit and we're now <= to it.
  size_t total_known_size_used =
      input_contents_size_ + output_size_used_;

  if (total_known_size_used <= total_buffer_size_ &&
      (total_known_size_used + bytes_consumed > total_buffer_size_) &&
      !space_available_callback_.is_null())
    space_available_callback_.Run();
}

void ByteStreamInputImpl::PostToPeer(
    bool complete, content::DownloadInterruptReason status) {
  DCHECK(my_task_runner_->RunsTasksOnCurrentThread());
  // Valid contexts in which to call.
  DCHECK(complete || 0 != input_contents_size_);

  scoped_ptr<ContentVector> transfer_buffer(new ContentVector);
  size_t buffer_size = 0;
  if (0 != input_contents_size_) {
    transfer_buffer.reset(new ContentVector);
    transfer_buffer->swap(input_contents_);
    buffer_size = input_contents_size_;
    output_size_used_ += input_contents_size_;
    input_contents_size_ = 0;
  }
  peer_task_runner_->PostTask(
      FROM_HERE, base::Bind(
          &ByteStreamOutputImpl::TransferData,
          peer_lifetime_flag_,
          peer_,
          base::Passed(transfer_buffer.Pass()),
          buffer_size,
          complete,
          status));
}

ByteStreamOutputImpl::ByteStreamOutputImpl(
    scoped_refptr<base::SequencedTaskRunner> task_runner,
    scoped_refptr<LifetimeFlag> lifetime_flag,
    size_t buffer_size)
    : total_buffer_size_(buffer_size),
      my_task_runner_(task_runner),
      my_lifetime_flag_(lifetime_flag),
      received_status_(false),
      status_(content::DOWNLOAD_INTERRUPT_REASON_NONE),
      unreported_consumed_bytes_(0),
      peer_(NULL) {
  DCHECK(my_lifetime_flag_.get());
  my_lifetime_flag_->is_alive_ = true;
}

ByteStreamOutputImpl::~ByteStreamOutputImpl() {
  my_lifetime_flag_->is_alive_ = false;
}

void ByteStreamOutputImpl::SetPeer(
    ByteStreamInputImpl* peer,
    scoped_refptr<base::SequencedTaskRunner> peer_task_runner,
    scoped_refptr<LifetimeFlag> peer_lifetime_flag) {
  peer_ = peer;
  peer_task_runner_ = peer_task_runner;
  peer_lifetime_flag_ = peer_lifetime_flag;
}

ByteStreamOutputImpl::StreamState
ByteStreamOutputImpl::Read(scoped_refptr<net::IOBuffer>* data,
                           size_t* length) {
  DCHECK(my_task_runner_->RunsTasksOnCurrentThread());

  if (available_contents_.size()) {
    *data = available_contents_.front().first;
    *length = available_contents_.front().second;
    available_contents_.pop_front();
    unreported_consumed_bytes_ += *length;

    MaybeUpdateInput();
    return STREAM_HAS_DATA;
  }
  if (received_status_) {
    return STREAM_COMPLETE;
  }
  return STREAM_EMPTY;
}

content::DownloadInterruptReason
ByteStreamOutputImpl::GetStatus() const {
  DCHECK(my_task_runner_->RunsTasksOnCurrentThread());
  DCHECK(received_status_);
  return status_;
}

void ByteStreamOutputImpl::RegisterCallback(
    const base::Closure& sink_callback) {
  DCHECK(my_task_runner_->RunsTasksOnCurrentThread());

  data_available_callback_ = sink_callback;
}

// static
void ByteStreamOutputImpl::TransferData(
    scoped_refptr<LifetimeFlag> object_lifetime_flag,
    ByteStreamOutputImpl* target,
    scoped_ptr<ContentVector> transfer_buffer,
    size_t buffer_size,
    bool source_complete,
    content::DownloadInterruptReason status) {
  // If our target is no longer alive, do nothing.
  if (!object_lifetime_flag->is_alive_) return;

  target->TransferDataInternal(
      transfer_buffer.Pass(), buffer_size, source_complete, status);
}

void ByteStreamOutputImpl::TransferDataInternal(
    scoped_ptr<ContentVector> transfer_buffer,
    size_t buffer_size,
    bool source_complete,
    content::DownloadInterruptReason status) {
  DCHECK(my_task_runner_->RunsTasksOnCurrentThread());

  bool was_empty = available_contents_.empty();

  if (transfer_buffer.get()) {
    available_contents_.insert(available_contents_.end(),
                                        transfer_buffer->begin(),
                                        transfer_buffer->end());
  }

  if (source_complete) {
    received_status_ = true;
    status_ = status;
  }

  // Callback on transition from empty to non-empty, or
  // source complete.
  if (((was_empty && !available_contents_.empty()) ||
       source_complete) &&
      !data_available_callback_.is_null())
    data_available_callback_.Run();
}

// Decide whether or not to send the input a window update.
// Currently we do that whenever we've got unreported consumption
// greater than 1/3 of total size.
void ByteStreamOutputImpl::MaybeUpdateInput() {
  DCHECK(my_task_runner_->RunsTasksOnCurrentThread());

  if (unreported_consumed_bytes_ <=
      total_buffer_size_ / kFractionReadBeforeWindowUpdate)
    return;

  peer_task_runner_->PostTask(
      FROM_HERE, base::Bind(
          &ByteStreamInputImpl::UpdateWindow,
          peer_lifetime_flag_,
          peer_,
          unreported_consumed_bytes_));
  unreported_consumed_bytes_ = 0;
}

}  // namespace

namespace content {

ByteStreamOutput::~ByteStreamOutput() { }

ByteStreamInput::~ByteStreamInput() { }

void CreateByteStream(
    scoped_refptr<base::SequencedTaskRunner> input_task_runner,
    scoped_refptr<base::SequencedTaskRunner> output_task_runner,
    size_t buffer_size,
    scoped_ptr<ByteStreamInput>* input,
    scoped_ptr<ByteStreamOutput>* output) {
  scoped_refptr<LifetimeFlag> input_flag(new LifetimeFlag());
  scoped_refptr<LifetimeFlag> output_flag(new LifetimeFlag());

  ByteStreamInputImpl* in = new ByteStreamInputImpl(
      input_task_runner, input_flag, buffer_size);
  ByteStreamOutputImpl* out = new ByteStreamOutputImpl(
      output_task_runner, output_flag, buffer_size);

  in->SetPeer(out, output_task_runner, output_flag);
  out->SetPeer(in, input_task_runner, input_flag);
  input->reset(in);
  output->reset(out);
}

}  // namespace content
