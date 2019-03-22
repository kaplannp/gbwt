/*
  Copyright (c) 2017, 2018, 2019 Jouni Siren
  Copyright (c) 2017 Genome Research Ltd.

  Author: Jouni Siren <jouni.siren@iki.fi>

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#include <gbwt/internal.h>

#include <functional>

namespace gbwt
{

//------------------------------------------------------------------------------

void
reversePath(vector_type& path)
{
  std::reverse(path.begin(), path.end());
  for(auto& node : path) { node = Node::reverse(node); }
}

void
reversePath(const vector_type& path, vector_type& output)
{
  for(auto iter = path.rbegin(); iter != path.rend(); ++iter) { output.push_back(Node::reverse(*iter)); }
}

void
reversePath(const vector_type& path, text_type& output, size_type& tail)
{
  for(auto iter = path.rbegin(); iter != path.rend(); ++iter) { output[tail] = Node::reverse(*iter); tail++; }
}

//------------------------------------------------------------------------------

rank_type
edgeTo(node_type to, const std::vector<edge_type>& outgoing)
{
  rank_type low = 0, high = outgoing.size();
  while(low < high)
  {
    rank_type mid = low + (high - low) / 2;
    if(outgoing[mid].first == to) { return mid; }
    if(outgoing[mid].first > to) { high = mid; }
    else { low = mid + 1; }
  }
  return outgoing.size();
}

DynamicRecord::DynamicRecord() :
  body_size(0)
{
}

void
DynamicRecord::clear()
{
  DynamicRecord temp;
  this->swap(temp);
}

void
DynamicRecord::swap(DynamicRecord& another)
{
  if(this != &another)
  {
    std::swap(this->body_size, another.body_size);
    this->incoming.swap(another.incoming);
    this->outgoing.swap(another.outgoing);
    this->body.swap(another.body);
    this->ids.swap(another.ids);
  }
}

//------------------------------------------------------------------------------

void
DynamicRecord::recode()
{
  if(this->empty()) { return; }

  bool sorted = true;
  for(rank_type outrank = 1; outrank < this->outdegree(); outrank++)
  {
    if(this->successor(outrank) < this->successor(outrank - 1)) { sorted = false; break; }
  }
  if(sorted) { return; }

  for(run_type& run : this->body) { run.first = this->successor(run.first); }
  sequentialSort(this->outgoing.begin(), this->outgoing.end());
  for(run_type& run : this->body) { run.first = this->edgeTo(run.first); }
}

void
DynamicRecord::removeUnusedEdges()
{
  // Determine which edges are used and replace the outranks with node identifiers.
  std::vector<bool> used(this->outdegree(), false);
  for(run_type& run : this->body)
  {
    used[run.first] = true;
    run.first = this->successor(run.first);
  }

  // Remove unused edges.
  size_type tail = 0;
  for(size_type i = 0; i < this->outdegree(); i++)
  {
    this->outgoing[tail] = this->outgoing[i];
    if(used[i]) { tail++; }
  }
  this->outgoing.resize(tail);

  // Recode the body.
  for(run_type& run : this->body) { run.first = this->edgeTo(run.first); }
}

void
DynamicRecord::writeBWT(std::vector<byte_type>& data) const
{
  // Write the outgoing edges.
  ByteCode::write(data, this->outdegree());
  node_type prev = 0;
  for(edge_type outedge : this->outgoing)
  {
    ByteCode::write(data, outedge.first - prev);
    prev = outedge.first;
    ByteCode::write(data, outedge.second);
  }

  // Write the body.
  if(this->outdegree() > 0)
  {
    Run encoder(this->outdegree());
    for(run_type run : this->body) { encoder.write(data, run); }
  }
}
//------------------------------------------------------------------------------

edge_type
DynamicRecord::LF(size_type i) const
{
  size_type run_end = 0;
  return this->runLF(i, run_end);
}

template<class Array>
edge_type LFLoop(Array& result, const std::vector<edge_type>& body, size_type i, size_type& run_end)
{
  rank_type last_edge = 0;
  size_type offset = 0;
  for(run_type run : body)
  {
    last_edge = run.first;
    result[run.first].second += run.second;
    offset += run.second;
    if(offset > i) { break; }
  }

  result[last_edge].second -= (offset - i);
  run_end = offset - 1;
  return result[last_edge];
}

edge_type
DynamicRecord::runLF(size_type i, size_type& run_end) const
{
  if(i >= this->size()) { return invalid_edge(); }

  if(this->outdegree() <= MAX_OUTDEGREE_FOR_ARRAY)
  {
    edge_type result[MAX_OUTDEGREE_FOR_ARRAY];
    for(size_type i = 0; i < this->outdegree(); i++) { result[i] = this->outgoing[i]; }
    return LFLoop(result, this->body, i, run_end);
  }
  else
  {
    std::vector<edge_type> result(this->outgoing);
    return LFLoop(result, this->body, i, run_end);
  }
}

// run is *(--iter); offset and result are for the beginning of the run at iter.
size_type
LFLoop(std::vector<run_type>::const_iterator& iter, std::vector<run_type>::const_iterator end,
       size_type i, rank_type outrank, run_type& run, size_type& offset, size_type& result)
{
  while(iter != end && offset < i)
  {
    run = *iter; ++iter;
    offset += run.second;
    if(run.first == outrank) { result += run.second; }
  }
  return result - (run.first == outrank ? offset - i : 0);
}

size_type
DynamicRecord::LF(size_type i, node_type to) const
{
  rank_type outrank = this->edgeTo(to);
  if(outrank >= this->outdegree()) { return invalid_offset(); }

  std::vector<run_type>::const_iterator iter = this->body.begin();
  run_type run(0, 0);
  size_type offset = 0, result = this->offset(outrank);

  return LFLoop(iter, this->body.end(), i, outrank, run, offset, result);
}

range_type
DynamicRecord::LF(range_type range, node_type to) const
{
  if(Range::empty(range)) { return Range::empty_range(); }

  rank_type outrank = this->edgeTo(to);
  if(outrank >= this->outdegree()) { return Range::empty_range(); }

  std::vector<run_type>::const_iterator iter = this->body.begin();
  run_type run(0, 0);
  size_type offset = 0, result = this->offset(outrank);

  // [LF(range.first, to), LF(range.second + 1, to) - 1].
  range.first = LFLoop(iter, this->body.end(), range.first, outrank, run, offset, result);
  range.second = LFLoop(iter, this->body.end(), range.second + 1, outrank, run, offset, result) - 1;
  return range;
}

range_type
DynamicRecord::bdLF(range_type range, node_type to, size_type& reverse_offset) const
{
  if(Range::empty(range)) { return Range::empty_range(); }

  rank_type outrank = this->edgeTo(to);
  if(outrank >= this->outdegree()) { return Range::empty_range(); }

  // sp = LF(range.first, to)
  std::vector<run_type>::const_iterator iter = this->body.begin();
  run_type run(0, 0);
  size_type offset = 0, result = this->offset(outrank);
  size_type sp = LFLoop(iter, this->body.end(), range.first, outrank, run, offset, result);

  /*
    Count the number of occurrences of nodes x in the query range, where
    Node::reverse(x) < Node::reverse(to), and store it in reverse_offset.

    1. In the easy case, there are no edges to Node::reverse(to), so we only compute
       the occurrences < outrank.
    2. If there are edges to Node::reverse(to) and to is in forward orientation, we
       count the occurrences <= reverse_rank except those of outrank.
    3. If there are edges to Node::reverse(to), and to is in reverse orientation, we
       count the occurrences < reverse_rank < outrank.
  */
  rank_type reverse_rank = this->edgeTo(Node::reverse(to));
  bool subtract_equal = false;
  if(reverse_rank >= this->outdegree()) { reverse_rank = outrank; }
  else if(!Node::is_reverse(to)) { reverse_rank++; subtract_equal = true; }

  // Previous run may go past range.first.
  size_type equal = (run.first == outrank ? offset - range.first : 0);
  reverse_offset = (run.first < reverse_rank ? offset - range.first : 0);

  // ep + 1 = LF(range.second + 1, to)
  range.second++;
  while(iter != this->body.end() && offset < range.second)
  {
    run = *iter; ++iter;
    offset += run.second;
    if(run.first == outrank) { equal += run.second; }
    if(run.first < reverse_rank) { reverse_offset += run.second; }
  }

  // Last run may go past range.second.
  if(run.first == outrank) { equal -= (offset - range.second); }
  if(run.first < reverse_rank) { reverse_offset -= (offset - range.second); }

  if(subtract_equal) { reverse_offset -= equal; }
  return range_type(sp, sp + equal - 1);
}

node_type
DynamicRecord::operator[](size_type i) const
{
  if(i >= this->size()) { return ENDMARKER; }

  size_type offset = 0;
  for(run_type run : this->body)
  {
    offset += run.second;
    if(offset > i) { return this->successor(run.first); }
  }

  return ENDMARKER;
}

//------------------------------------------------------------------------------

bool
DynamicRecord::hasEdge(node_type to) const
{
  for(rank_type outrank = 0; outrank < this->outdegree(); outrank++)
  {
    if(this->successor(outrank) == to) { return true; }
  }
  return false;
}

rank_type
DynamicRecord::edgeToLinear(node_type to) const
{
  for(rank_type outrank = 0; outrank < this->outdegree(); outrank++)
  {
    if(this->successor(outrank) == to) { return outrank; }
  }
  return this->outdegree();
}

//------------------------------------------------------------------------------

size_type
DynamicRecord::countBefore(node_type from) const
{
  size_type result = 0;
  for(rank_type inrank = 0; inrank < this->indegree() && this->predecessor(inrank) < from; inrank++)
  {
    result += this->count(inrank);
  }
  return result;
}

size_type
DynamicRecord::countUntil(node_type from) const
{
  size_type result = 0;
  for(rank_type inrank = 0; inrank < this->indegree() && this->predecessor(inrank) <= from; inrank++)
  {
    result += this->count(inrank);
  }
  return result;
}

void
DynamicRecord::increment(node_type from)
{
  for(rank_type inrank = 0; inrank < this->indegree(); inrank++)
  {
    if(this->predecessor(inrank) == from) { this->count(inrank)++; return; }
  }
  this->addIncoming(edge_type(from, 1));
}

void
DynamicRecord::addIncoming(edge_type inedge)
{
  this->incoming.push_back(inedge);
  sequentialSort(this->incoming.begin(), this->incoming.end());
}

//------------------------------------------------------------------------------

std::vector<sample_type>::const_iterator
DynamicRecord::nextSample(size_type i) const
{
  std::vector<sample_type>::const_iterator curr = this->ids.begin();
  while(curr != this->ids.end() && curr->first < i) { ++curr; }
  return curr;
}

//------------------------------------------------------------------------------

std::ostream&
operator<<(std::ostream& out, const DynamicRecord& record)
{
  out << "(size " << record.size() << ", "
      << record.runs() << " runs, "
      << "indegree " << record.indegree()
      << ", outdegree " << record.outdegree()
      << ", incoming = " << record.incoming
      << ", outgoing = " << record.outgoing
      << ", body = " << record.body
      << ", ids = " << record.ids << ")";

  return out;
}

//------------------------------------------------------------------------------

CompressedRecord::CompressedRecord() :
  outgoing(), body(0), data_size(0)
{
}

CompressedRecord::CompressedRecord(const std::vector<byte_type>& source, size_type start, size_type limit)
{
  this->outgoing.resize(ByteCode::read(source, start));
  node_type prev = 0;
  for(edge_type& outedge : this->outgoing)
  {
    outedge.first = ByteCode::read(source, start) + prev;
    prev = outedge.first;
    outedge.second = ByteCode::read(source, start);
  }

  this->body = source.data() + start;
  this->data_size = limit - start;
}

bool
CompressedRecord::emptyRecord(const std::vector<byte_type>& source, size_type start)
{
  return (ByteCode::read(source, start) == 0);
}

size_type
CompressedRecord::size() const
{
  size_type result = 0;
  if(this->outdegree() > 0)
  {
    for(CompressedRecordIterator iter(*this); !(iter.end()); ++iter) { result += iter->second; }
  }
  return result;
}

size_type
CompressedRecord::runs() const
{
  size_type result = 0;
  if(this->outdegree() > 0)
  {
    for(CompressedRecordIterator iter(*this); !(iter.end()); ++iter) { result++; }
  }
  return result;
}

edge_type
CompressedRecord::LF(size_type i) const
{
  size_type run_end = 0;
  return this->runLF(i, run_end);
}

edge_type
CompressedRecord::runLF(size_type i, size_type& run_end) const
{
  if(this->outdegree() == 0) { return invalid_edge(); }

  if(this->outdegree() <= MAX_OUTDEGREE_FOR_ARRAY)
  {
    CompressedRecordArrayIterator iter(*this);
    edge_type result = iter.edgeAt(i);
    if(result != invalid_edge()) { run_end = iter.offset() - 1; }
    return result;
  }
  else
  {
    CompressedRecordFullIterator iter(*this);
    edge_type result = iter.edgeAt(i);
    if(result != invalid_edge()) { run_end = iter.offset() - 1; }
    return result;
  }
}

size_type
CompressedRecord::LF(size_type i, node_type to) const
{
  size_type outrank = this->edgeTo(to);
  if(outrank >= this->outdegree()) { return invalid_offset(); }
  CompressedRecordRankIterator iter(*this, outrank);
  return iter.rankAt(i);
}

range_type
CompressedRecord::LF(range_type range, node_type to) const
{
  if(Range::empty(range)) { return Range::empty_range(); }

  size_type outrank = this->edgeTo(to);
  if(outrank >= this->outdegree()) { return Range::empty_range(); }
  CompressedRecordRankIterator iter(*this, outrank);
  range.first = iter.rankAt(range.first);
  range.second = iter.rankAt(range.second + 1) - 1;

  return range;
}

range_type
CompressedRecord::bdLF(range_type range, node_type to, size_type& reverse_offset) const
{
  if(Range::empty(range)) { return Range::empty_range(); }

  size_type outrank = this->edgeTo(to);
  if(outrank >= this->outdegree()) { return Range::empty_range(); }

  CompressedRecordRankIterator iter(*this, outrank);
  size_type sp = iter.rankAt(range.first);

  /*
    Count the number of occurrences of nodes x in the query range, where
    Node::reverse(x) < Node::reverse(to), and store it in reverse_offset.

    1. In the easy case, there are no edges to Node::reverse(to), so we only compute
       the occurrences < outrank.
    2. If there are edges to Node::reverse(to) and to is in forward orientation, we
       count the occurrences <= reverse_rank except those of outrank.
    3. If there are edges to Node::reverse(to), and to is in reverse orientation, we
       count the occurrences < reverse_rank < outrank.
  */
  rank_type reverse_rank = this->edgeTo(Node::reverse(to));
  if(reverse_rank >= this->outdegree()) { reverse_rank = outrank; }
  else if(!Node::is_reverse(to)) { reverse_rank++; }

  // Previous run may go past range.first.
  if(iter->first < reverse_rank && iter->first != outrank) { reverse_offset = iter.offset() - range.first; }
  else { reverse_offset = 0; }

  range.second++; // We compute rank at range.second + 1.
  while(!(iter.end()) && iter.offset() < range.second)
  {
    ++iter;
    if(iter->first < reverse_rank && iter->first != outrank) { reverse_offset += iter->second; }
  }

  // Last run may go past range.second.
  if(iter->first < reverse_rank && iter->first != outrank) { reverse_offset -= (iter.offset() - range.second); }

  return range_type(sp, iter.rankAt(range.second) - 1);
}

node_type
CompressedRecord::operator[](size_type i) const
{
  if(this->outdegree() == 0) { return ENDMARKER; }

  for(CompressedRecordIterator iter(*this); !(iter.end()); ++iter)
  {
    if(iter.offset() > i) { return this->successor(iter->first); }
  }
  return ENDMARKER;
}

bool
CompressedRecord::hasEdge(node_type to) const
{
  for(rank_type outrank = 0; outrank < this->outdegree(); outrank++)
  {
    if(this->successor(outrank) == to) { return true; }
  }
  return false;
}

//------------------------------------------------------------------------------

DecompressedRecord::DecompressedRecord() :
  outgoing(), after(), body()
{
}

DecompressedRecord::DecompressedRecord(const DynamicRecord& source) :
  outgoing(source.outgoing), after(source.outgoing), body()
{
  this->body.reserve(source.size());
  for(run_type run : source.body)
  {
    for(size_type i = 0; i < run.second; i++)
    {
      this->body.push_back(this->after[run.first]);
      this->after[run.first].second++;
    }
  }
}

DecompressedRecord::DecompressedRecord(const CompressedRecord& source) :
  outgoing(source.outgoing), after(source.outgoing), body()
{
  this->body.reserve(source.size());
  for(CompressedRecordIterator iter(source); !(iter.end()); ++iter)
  {
    for(size_type i = 0; i < iter->second; i++)
    {
      this->body.push_back(this->after[iter->first]);
      this->after[iter->first].second++;
    }
  }
}

size_type
DecompressedRecord::runs() const
{
  if(this->empty()) { return 0; }

  size_type result = 0;
  node_type prev = invalid_node();
  for(edge_type edge : body)
  {
    if(edge.first != prev) { result++; prev = edge.first; }
  }

  return result;
}

edge_type
DecompressedRecord::LF(size_type i) const
{
  if(i >= this->size()) { return invalid_edge(); }
  return this->body[i];
}

edge_type
DecompressedRecord::runLF(size_type i, size_type& run_end) const
{
  if(i >= this->size()) { return invalid_edge(); }

  run_end = i;
  while(run_end + 1 < this->size() && this->body[run_end + 1].first == this->body[i].first) { run_end++; }

  return this->body[i];
}

node_type
DecompressedRecord::operator[](size_type i) const
{
  if(i >= this->size()) { return ENDMARKER; }
  return this->body[i].first;
}

bool
DecompressedRecord::hasEdge(node_type to) const
{
  for(rank_type outrank = 0; outrank < this->outdegree(); outrank++)
  {
    if(this->successor(outrank) == to) { return true; }
  }
  return false;
}

//------------------------------------------------------------------------------

RecordArray::RecordArray() :
  records(0)
{
}

RecordArray::RecordArray(const RecordArray& source)
{
  this->copy(source);
}

RecordArray::RecordArray(RecordArray&& source)
{
  *this = std::move(source);
}

RecordArray::~RecordArray()
{
}

RecordArray::RecordArray(const std::vector<DynamicRecord>& bwt) :
  records(bwt.size())
{
  // Find the starting offsets and compress the BWT.
  std::vector<size_type> offsets(bwt.size());
  for(size_type i = 0; i < bwt.size(); i++)
  {
    offsets[i] = this->data.size();
    bwt[i].writeBWT(this->data);
  }

  this->buildIndex(offsets);
}

RecordArray::RecordArray(const std::vector<RecordArray const*> sources, const sdsl::int_vector<0>& origins, const std::vector<size_type>& record_offsets) :
  records(origins.size())
{
  size_type data_size = 0;
  for(auto source : sources) { data_size += source->data.size(); }

  // Merge the endmarkers.
  std::vector<size_type> limits(sources.size(), 0); // Pointers to the end of the current records.
  {
    DynamicRecord merged;
    for(size_type i = 0; i < sources.size(); i++)
    {
      if(sources[i]->empty()) { continue; }
      size_type start = sources[i]->start(ENDMARKER), limit = sources[i]->limit(ENDMARKER);
      CompressedRecord record(sources[i]->data, start, limit);
      for(CompressedRecordIterator iter(record); !(iter.end()); ++iter)
      {
        run_type run = *iter; run.first += merged.outdegree();
        merged.body.push_back(run); merged.body_size += run.second;
      }
      for(edge_type outedge : record.outgoing)
      {
        merged.outgoing.push_back(outedge);
      }
      limits[i] = limit;
    }
    merged.recode();
    merged.writeBWT(this->data);
  }

  // Merge the BWTs.
  this->data.reserve(data_size + this->data.size());
  std::vector<size_type> offsets(origins.size(), 0);
  for(comp_type comp = 1; comp < origins.size(); comp++)
  {
    offsets[comp] = this->data.size();
    size_type origin = origins[comp];
    if(origin >= sources.size())
    {
      this->data.push_back(0);  // Empty record, outdegree 0.
      continue;
    }
    size_type start = limits[origin], limit = sources[origin]->limit(comp - record_offsets[origin]);
    limits[origin] = limit;
    for(size_type i = start; i < limit; i++)
    {
      this->data.push_back(sources[origin]->data[i]);
    }
  }

  // Build the index for the BWT.
  this->buildIndex(offsets);
}


RecordArray::RecordArray(size_type array_size) :
  records(array_size)
{  
}

void
RecordArray::buildIndex(const std::vector<size_type>& offsets)
{
  sdsl::sd_vector_builder builder(this->data.size(), offsets.size());
  for(size_type offset : offsets) { builder.set(offset); }
  this->index = sdsl::sd_vector<>(builder);
  sdsl::util::init_support(this->select, &(this->index));
}

void
RecordArray::swap(RecordArray& another)
{
  if(this != &another)
  {
    std::swap(this->records, another.records),
    this->index.swap(another.index);
    sdsl::util::swap_support(this->select, another.select, &(this->index), &(another.index));
    this->data.swap(another.data);
  }
}

RecordArray&
RecordArray::operator=(const RecordArray& source)
{
  if(this != &source) { this->copy(source); }
  return *this;
}

RecordArray&
RecordArray::operator=(RecordArray&& source)
{
  if(this != &source)
  {
    this->records = std::move(source.records);
    this->index = std::move(source.index);
    this->select = std::move(source.select); this->select.set_vector(&(this->index));
    this->data = std::move(source.data);
  }
  return *this;
}

size_type
RecordArray::serialize(std::ostream& out, sdsl::structure_tree_node* v, std::string name) const
{
  sdsl::structure_tree_node* child = sdsl::structure_tree::add_child(v, name, sdsl::util::class_name(*this));
  size_type written_bytes = 0;

  written_bytes += sdsl::write_member(this->records, out, child, "records");
  written_bytes += this->index.serialize(out, child, "index");
  written_bytes += this->select.serialize(out, child, "select");

  // Serialize the data.
  size_type data_bytes = this->data.size() * sizeof(byte_type);
  sdsl::structure_tree_node* data_node =
    sdsl::structure_tree::add_child(child, "data", "std::vector<gbwt::byte_type>");
  if(this->data.size() > 0) { DiskIO::write(out, this->data.data(), this->data.size()); }
  sdsl::structure_tree::add_size(data_node, data_bytes);
  written_bytes += data_bytes;

  sdsl::structure_tree::add_size(child, written_bytes);
  return written_bytes;
}

void
RecordArray::load(std::istream& in)
{
  sdsl::read_member(this->records, in);

  // Read the record index.
  this->index.load(in);
  this->select.load(in, &(this->index));

  // Read the data.
  this->data.resize(this->index.size());
  if(this->data.size() > 0) { DiskIO::read(in, this->data.data(), this->data.size()); }
}

void
RecordArray::copy(const RecordArray& source)
{
  this->records = source.records;
  this->index = source.index;
  this->select = source.select; this->select.set_vector(&(this->index));
  this->data = source.data;
}

//------------------------------------------------------------------------------

DASamples::DASamples()
{
}

DASamples::DASamples(const DASamples& source)
{
  this->copy(source);
}

DASamples::DASamples(DASamples&& source)
{
  *this = std::move(source);
}

DASamples::~DASamples()
{
}

DASamples::DASamples(const std::vector<DynamicRecord>& bwt)
{
  // Determine the statistics and mark the sampled nodes.
  size_type record_count = 0, bwt_offsets = 0, sample_count = 0;
  this->sampled_records = sdsl::bit_vector(bwt.size(), 0);
  for(size_type i = 0; i < bwt.size(); i++)
  {
    if(bwt[i].samples() > 0)
    {
      record_count++; bwt_offsets += bwt[i].size(); sample_count += bwt[i].samples();
      this->sampled_records[i] = 1;
    }
  }
  sdsl::util::init_support(this->record_rank, &(this->sampled_records));

  // Build the bitvectors over BWT offsets.
  sdsl::sd_vector_builder range_builder(bwt_offsets, record_count);
  sdsl::sd_vector_builder offset_builder(bwt_offsets, sample_count);
  size_type offset = 0, max_sample = 0;
  for(const DynamicRecord& record : bwt)
  {
    if(record.samples() > 0)
    {
      range_builder.set(offset);
      for(sample_type sample : record.ids)
      {
        offset_builder.set(offset + sample.first);
        max_sample = std::max(max_sample, (size_type)(sample.second));
      }
      offset += record.size();
    }
  }
  this->bwt_ranges = sdsl::sd_vector<>(range_builder);
  sdsl::util::init_support(this->bwt_select, &(this->bwt_ranges));
  this->sampled_offsets = sdsl::sd_vector<>(offset_builder);
  sdsl::util::init_support(this->sample_rank, &(this->sampled_offsets));

  // Store the samples.
  this->array = sdsl::int_vector<0>(sample_count, 0, bit_length(max_sample));
  size_type curr = 0;
  for(const DynamicRecord& record : bwt)
  {
    if(record.samples() > 0)
    {
      for(sample_type sample : record.ids) { this->array[curr] = sample.second; curr++; }
    }
  }
}

DASamples::DASamples(const std::vector<DASamples const*> sources, const sdsl::int_vector<0>& origins, const std::vector<size_type>& record_offsets, const std::vector<size_type>& sequence_counts)
{
  // Compute statistics and build iterators over the sources.
  size_type sample_count = 0, total_sequences = 0;
  std::vector<size_type> sequence_offsets(sources.size(), 0);
  std::vector<SampleIterator> sample_iterators;
  std::vector<SampleRangeIterator> range_iterators;
  for(size_type i = 0; i < sources.size(); i++)
  {
    sample_count += sources[i]->size();
    sequence_offsets[i] = total_sequences;
    total_sequences += sequence_counts[i];
    sample_iterators.push_back(SampleIterator(*(sources[i])));
    range_iterators.push_back(SampleRangeIterator(*(sources[i])));
  }

  // Compute statistics over the records and mark the sampled nodes.
  // Note that the endmarker requires special treatment.
  size_type record_count = 0, bwt_offsets = 0;
  this->sampled_records = sdsl::bit_vector(origins.size(), 0);
  bool sample_endmarker = false;
  for(size_type origin = 0; origin < sources.size(); origin++)
  {
    if(sources[origin]->isSampled(ENDMARKER))
    {
      sample_endmarker = true;
      ++range_iterators[origin];
    }
  }
  if(sample_endmarker)
  {
    record_count++;
    bwt_offsets += total_sequences;
    this->sampled_records[ENDMARKER] = 1;
  }
  for(size_type i = 1; i < origins.size(); i++)
  {
    size_type origin = origins[i];
    if(origin >= sources.size()) { continue; }  // No record.
    if(sources[origin]->isSampled(i - record_offsets[origin]))
    {
      record_count++;
      bwt_offsets += range_iterators[origin].length();
      this->sampled_records[i] = 1;
      ++range_iterators[origin];
    }
  }
  sdsl::util::init_support(this->record_rank, &(this->sampled_records));

  // Reset the range iterators.
  range_iterators.clear();
  for(size_type i = 0; i < sources.size(); i++)
  {
    range_iterators.push_back(SampleRangeIterator(*(sources[i])));
  }

  // Build the bitvectors over BWT offsets and store the samples.
  // The endmarker requires special treatment again.
  sdsl::sd_vector_builder range_builder(bwt_offsets, record_count);
  sdsl::sd_vector_builder offset_builder(bwt_offsets, sample_count);
  this->array = sdsl::int_vector<0>(sample_count, 0, bit_length(total_sequences - 1));
  size_type record_start = 0, curr = 0;
  if(sample_endmarker)
  {
    range_builder.set(record_start);
    for(size_type origin = 0; origin < sources.size(); origin++)
    {
      if(!(sources[origin]->isSampled(ENDMARKER))) { continue; }
      while(!(sample_iterators[origin].end()) && sample_iterators[origin].offset() < range_iterators[origin].limit())
      {
        offset_builder.set((sample_iterators[origin]).offset() + sequence_offsets[origin]);
        this->array[curr] = *(sample_iterators[origin]) + sequence_offsets[origin]; curr++;
        ++sample_iterators[origin];
      }
      ++range_iterators[origin];
    }
    record_start += total_sequences;
  }
  for(size_type i = 1; i < origins.size(); i++)
  {
    if(!(this->isSampled(i))) { continue; }
    size_type origin = origins[i];
    range_builder.set(record_start);
    while(!(sample_iterators[origin].end()) && sample_iterators[origin].offset() < range_iterators[origin].limit())
    {
      offset_builder.set((sample_iterators[origin].offset() - range_iterators[origin].start()) + record_start);
      this->array[curr] = *(sample_iterators[origin]) + sequence_offsets[origin]; curr++;
      ++sample_iterators[origin];
    }
    record_start += range_iterators[origin].length();
    ++range_iterators[origin];
  }
  this->bwt_ranges = sdsl::sd_vector<>(range_builder);
  sdsl::util::init_support(this->bwt_select, &(this->bwt_ranges));
  this->sampled_offsets = sdsl::sd_vector<>(offset_builder);
  sdsl::util::init_support(this->sample_rank, &(this->sampled_offsets));
}

void
DASamples::swap(DASamples& another)
{
  if(this != &another)
  {
    this->sampled_records.swap(another.sampled_records);
    sdsl::util::swap_support(this->record_rank, another.record_rank, &(this->sampled_records), &(another.sampled_records));

    this->bwt_ranges.swap(another.bwt_ranges);
    sdsl::util::swap_support(this->bwt_select, another.bwt_select, &(this->bwt_ranges), &(another.bwt_ranges));

    this->sampled_offsets.swap(another.sampled_offsets);
    sdsl::util::swap_support(this->sample_rank, another.sample_rank, &(this->sampled_offsets), &(another.sampled_offsets));

    this->array.swap(another.array);
  }
}

DASamples&
DASamples::operator=(const DASamples& source)
{
  if(this != &source) { this->copy(source); }
  return *this;
}

DASamples&
DASamples::operator=(DASamples&& source)
{
  if(this != &source)
  {
    this->sampled_records = std::move(source.sampled_records);
    this->record_rank = std::move(source.record_rank);

    this->bwt_ranges = std::move(source.bwt_ranges);
    this->bwt_select = std::move(source.bwt_select);

    this->sampled_offsets = std::move(source.sampled_offsets);
    this->sample_rank = std::move(source.sample_rank);

    this->array = std::move(source.array);

    this->setVectors();
  }
  return *this;
}

size_type
DASamples::serialize(std::ostream& out, sdsl::structure_tree_node* v, std::string name) const
{
  sdsl::structure_tree_node* child = sdsl::structure_tree::add_child(v, name, sdsl::util::class_name(*this));
  size_type written_bytes = 0;

  written_bytes += this->sampled_records.serialize(out, child, "sampled_records");
  written_bytes += this->record_rank.serialize(out, child, "record_rank");

  written_bytes += this->bwt_ranges.serialize(out, child, "bwt_ranges");
  written_bytes += this->bwt_select.serialize(out, child, "bwt_select");

  written_bytes += this->sampled_offsets.serialize(out, child, "sampled_offsets");
  written_bytes += this->sample_rank.serialize(out, child, "sample_rank");

  written_bytes += this->array.serialize(out, child, "array");

  sdsl::structure_tree::add_size(child, written_bytes);
  return written_bytes;
}

void
DASamples::load(std::istream& in)
{
  this->sampled_records.load(in);
  this->record_rank.load(in, &(this->sampled_records));

  this->bwt_ranges.load(in);
  this->bwt_select.load(in, &(this->bwt_ranges));

  this->sampled_offsets.load(in);
  this->sample_rank.load(in, &(this->sampled_offsets));

  this->array.load(in);
}

void
DASamples::copy(const DASamples& source)
{
  this->sampled_records = source.sampled_records;
  this->record_rank = source.record_rank;

  this->bwt_ranges = source.bwt_ranges;
  this->bwt_select = source.bwt_select;

  this->sampled_offsets = source.sampled_offsets;
  this->sample_rank = source.sample_rank;

  this->array = source.array;

  this->setVectors();
}

void
DASamples::setVectors()
{
  this->record_rank.set_vector(&(this->sampled_records));
  this->bwt_select.set_vector(&(this->bwt_ranges));
  this->sample_rank.set_vector(&(this->sampled_offsets));
}

size_type
DASamples::tryLocate(size_type record, size_type offset) const
{
  if(!(this->isSampled(record))) { return invalid_sequence(); }

  size_type record_start = this->start(record);
  if(this->sampled_offsets[record_start + offset])
  {
    return this->array[this->sample_rank(record_start + offset)];
  }
  return invalid_sequence();
}

sample_type
DASamples::nextSample(size_type record, size_type offset) const
{
  if(!(this->isSampled(record))) { return invalid_sample(); }

  size_type record_start = this->start(record);
  size_type rank = this->sample_rank(record_start + offset);
  if(rank < this->array.size())
  {
    sdsl::sd_vector<>::select_1_type sample_select(&(this->sampled_offsets));
    return sample_type(sample_select(rank + 1) - record_start, this->array[rank]);
  }
  return invalid_sample();
}

size_type
DASamples::limit(size_type rank) const
{
  size_type rank_limit = this->record_rank(this->sampled_records.size());
  return (rank + 1 < rank_limit ? this->bwt_select(rank + 2) : this->bwt_ranges.size());
}

//------------------------------------------------------------------------------

MergeParameters::MergeParameters() :
  pos_buffer_size(POS_BUFFER_SIZE), thread_buffer_size(THREAD_BUFFER_SIZE),
  merge_buffers(MERGE_BUFFERS), chunk_size(CHUNK_SIZE), merge_jobs(MERGE_JOBS)
{
}

void
MergeParameters::setPosBufferSize(size_type megabytes)
{
  this->pos_buffer_size = Range::bound(megabytes, 1, MAX_BUFFER_SIZE);
}

void
MergeParameters::setThreadBufferSize(size_type megabytes)
{
  this->thread_buffer_size = Range::bound(megabytes, 1, MAX_BUFFER_SIZE);
}

void
MergeParameters::setMergeBuffers(size_type n)
{
  this->merge_buffers = Range::bound(n, 1, MAX_MERGE_BUFFERS);
}

void
MergeParameters::setChunkSize(size_type n)
{
  this->chunk_size = std::max(n, static_cast<size_type>(1));
}

void
MergeParameters::setMergeJobs(size_type n)
{
  this->merge_jobs = Range::bound(n, 1, MAX_MERGE_JOBS);
}

//------------------------------------------------------------------------------

Dictionary::Dictionary() :
  offsets(1, 0), sorted_ids(), data()
{
}

Dictionary::Dictionary(const Dictionary& source)
{
  this->copy(source);
}

Dictionary::Dictionary(Dictionary&& source)
{
  *this = std::move(source);
}

Dictionary::~Dictionary()
{
}

Dictionary::Dictionary(const std::vector<std::string>& source)
{
  if(source.empty())
  {
    *this = Dictionary();
    return;
  }

  size_type total_length = 0;
  for(const std::string& s : source) { total_length += s.length(); }
  this->offsets = sdsl::int_vector<0>(source.size() + 1, 0, bit_length(total_length));
  this->sorted_ids = sdsl::int_vector<0>(source.size(), 0, bit_length(source.size() - 1));
  this->data.reserve(total_length);

  // Initialize the arrays.
  size_type offset = 0;
  for(size_type i = 0; i < source.size(); i++)
  {
    this->offsets[i] = offset;
    this->sorted_ids[i] = i;
    this->data.insert(this->data.end(), source[i].begin(), source[i].end());
    offset += source[i].length();
  }
  this->offsets[source.size()] = total_length;

  // Sort sorted_ids.
  sequentialSort(this->sorted_ids.begin(), this->sorted_ids.end(), [this](size_type a, size_type b) -> bool
  {
    return this->smaller(a, b);
  });

  // Check for duplicates.
  for(size_type i = 0; i + 1 < this->size(); i++)
  {
    if(!(this->smaller(i, i + 1)))
    {
      std::cerr << "Dictionary::Dictionary(): Warning: The dictionary contains duplicate strings" << std::endl;
      break;
    }
  }
}

void
Dictionary::swap(Dictionary& another)
{
  if(this != &another)
  {
    this->offsets.swap(another.offsets);
    this->sorted_ids.swap(another.sorted_ids);
    this->data.swap(another.data);
  }
}

Dictionary&
Dictionary::operator=(const Dictionary& source)
{
  if(this != &source) { this->copy(source); }
  return *this;
}

Dictionary&
Dictionary::operator=(Dictionary&& source)
{
  if(this != &source)
  {
    this->offsets = std::move(source.offsets);
    this->sorted_ids = std::move(source.sorted_ids);
    this->data = std::move(source.data);
  }
  return *this;
}

size_type
Dictionary::serialize(std::ostream& out, sdsl::structure_tree_node* v, std::string name) const
{
  sdsl::structure_tree_node* child = sdsl::structure_tree::add_child(v, name, sdsl::util::class_name(*this));
  size_type written_bytes = 0;

  written_bytes += this->offsets.serialize(out, child, "offsets");
  written_bytes += this->sorted_ids.serialize(out, child, "sorted_ids");
  written_bytes += serializeVector(this->data, out, child, "data");

  sdsl::structure_tree::add_size(child, written_bytes);
  return written_bytes;
}

void
Dictionary::load(std::istream& in)
{
  this->offsets.load(in);
  this->sorted_ids.load(in);
  loadVector(this->data, in);
}

void
Dictionary::copy(const Dictionary& source)
{
  this->offsets = source.offsets;
  this->sorted_ids = source.sorted_ids;
  this->data = source.data;
}

bool
Dictionary::operator==(const Dictionary& another) const
{
  return (this->offsets == another.offsets && this->sorted_ids == another.sorted_ids && this->data == another.data);
}

void
Dictionary::clear()
{
  *this = Dictionary();
}

size_type
Dictionary::find(const std::string& s) const
{
  size_type start = 0, limit = this->size();
  while(start < limit)
  {
    size_type mid = start + (limit - start) / 2;
    if(this->smaller(s, mid)) { limit = mid; }
    else if(this->smaller(mid, s)) { start = mid + 1; }
    else { return this->sorted_ids[mid]; }
  }
  return this->size();
}

void
Dictionary::append(const Dictionary& source)
{
  if(source.empty()) { return; }

  size_type old_data_size = this->data.size();
  size_type old_size = this->size();
  size_type new_size = this->size() + source.size();

  // Concatenate the sequences.
  {
    std::vector<char> new_data; new_data.reserve(this->data.size() + source.data.size());
    new_data.insert(new_data.end(), this->data.begin(), this->data.end());
    new_data.insert(new_data.end(), source.data.begin(), source.data.end());
    this->data.swap(new_data);
  }

  // Concatenate the starting offsets
  {
    sdsl::int_vector<0> new_offsets(old_size + source.size() + 1, 0, bit_length(this->data.size()));
    for(size_type i = 0; i < old_size; i++) { new_offsets[i] = this->offsets[i]; }
    for(size_type i = 0; i <= source.size(); i++) { new_offsets[this->size() + 1] = old_data_size + source.offsets[i]; }
    this->offsets.swap(new_offsets);
  }

  // Rebuild sorted ids.
  this->sorted_ids = sdsl::int_vector<0>(new_size, 0, bit_length(new_size - 1));
  for(size_type i = 0; i < this->sorted_ids.size(); i++) { this->sorted_ids[i] = i; }
  sequentialSort(this->sorted_ids.begin(), this->sorted_ids.end(), [this](size_type a, size_type b) -> bool
  {
    return this->smaller(a, b);
  });

  // Check for duplicates.
  for(size_type i = 0; i + 1 < this->size(); i++)
  {
    if(!(this->smaller(i, i + 1)))
    {
      std::cerr << "Dictionary::append(): Warning: The dictionary contains duplicate strings" << std::endl;
      break;
    }
  }
}

template<class AIter, class BIter>
bool
stringCompare(AIter a_pos, AIter a_lim, BIter b_pos, BIter b_lim)
{
  while(a_pos != a_lim && b_pos != b_lim)
  {
    if(*a_pos != *b_pos) { return (*a_pos < *b_pos); }
    ++a_pos; ++b_pos;
  }
  return (a_pos == a_lim && b_pos != b_lim);
}

bool
Dictionary::smaller(size_type a, size_type b) const
{
  return stringCompare(this->data.begin() + this->offsets[this->sorted_ids[a]],
                       this->data.begin() + this->offsets[this->sorted_ids[a] + 1],
                       this->data.begin() + this->offsets[this->sorted_ids[b]],
                       this->data.begin() + this->offsets[this->sorted_ids[b] + 1]);
}

bool
Dictionary::smaller(size_type a, const std::string& b) const
{
  return stringCompare(this->data.begin() + this->offsets[this->sorted_ids[a]],
                       this->data.begin() + this->offsets[this->sorted_ids[a] + 1],
                       b.begin(),
                       b.end());
}

bool
Dictionary::smaller(const std::string& a, size_type b) const
{
  return stringCompare(a.begin(),
                       a.end(),
                       this->data.begin() + this->offsets[this->sorted_ids[b]],
                       this->data.begin() + this->offsets[this->sorted_ids[b] + 1]);
}

//------------------------------------------------------------------------------

Metadata::Metadata() :
  tag(TAG), version(VERSION),
  sample_count(0), haplotype_count(0), contig_count(0),
  flags(0)
{
}

size_type
Metadata::serialize(std::ostream& out, sdsl::structure_tree_node* v, std::string name) const
{
  sdsl::structure_tree_node* child = sdsl::structure_tree::add_child(v, name, sdsl::util::class_name(*this));
  size_type written_bytes = 0;

  written_bytes += sdsl::write_member(this->tag, out, child, "tag");
  written_bytes += sdsl::write_member(this->version, out, child, "version");
  written_bytes += sdsl::write_member(this->sample_count, out, child, "sample_count");
  written_bytes += sdsl::write_member(this->haplotype_count, out, child, "haplotype_count");
  written_bytes += sdsl::write_member(this->contig_count, out, child, "contig_count");
  written_bytes += sdsl::write_member(this->flags, out, child, "flags");

  if(this->hasPathNames())
  {
    written_bytes += serializeVector(this->path_names, out, child, "path_names");
  }
  if(this->hasSampleNames())
  {
    written_bytes += this->sample_names.serialize(out, child, "sample_names");
  }
  if(this->hasContigNames())
  {
    written_bytes += this->contig_names.serialize(out, child, "contig_names");
  }

  sdsl::structure_tree::add_size(child, written_bytes);
  return written_bytes;
}

void
Metadata::load(std::istream& in)
{
  sdsl::read_member(this->tag, in);
  sdsl::read_member(this->version, in);
  sdsl::read_member(this->sample_count, in);
  sdsl::read_member(this->haplotype_count, in);
  sdsl::read_member(this->contig_count, in);
  sdsl::read_member(this->flags, in);

  if(this->hasPathNames())
  {
    loadVector(this->path_names, in);
  }
  if(this->hasSampleNames())
  {
    this->sample_names.load(in);
  }
  if(this->hasContigNames())
  {
    this->contig_names.load(in);
  }
}

bool
Metadata::check() const
{
  if(this->tag != TAG) { return false; }
  switch(this->version)
  {
  case VERSION:
    return ((this->flags & FLAG_MASK) == this->flags);
  case INITIAL_VERSION:
    return ((this->flags & INITIAL_FLAG_MASK) == this->flags);
  default:
    return false;
  }
}

void
Metadata::swap(Metadata& another)
{
  if(this != &another)
  {
    std::swap(this->tag, another.tag);
    std::swap(this->version, another.version);
    std::swap(this->sample_count, another.sample_count);
    std::swap(this->haplotype_count, another.haplotype_count);
    std::swap(this->contig_count, another.contig_count);
    std::swap(this->flags, another.flags);

    this->path_names.swap(another.path_names);
    this->sample_names.swap(another.sample_names);
    this->contig_names.swap(another.contig_names);
  }
}

bool
Metadata::operator==(const Metadata& another) const
{
  return (this->tag == another.tag &&
          this->version == another.version &&
          this->sample_count == another.sample_count &&
          this->haplotype_count == another.haplotype_count &&
          this->contig_count == another.contig_count &&
          this->flags == another.flags &&
          this->path_names == another.path_names &&
          this->sample_names == another.sample_names &&
          this->contig_names == another.contig_names);
}

void
Metadata::setSamples(size_type n)
{
  if(this->hasSampleNames())
  {
    std::cerr << "Metadata::setSamples(): Warning: Changing sample count without changing sample names" << std::endl;
  }
  this->sample_count = n;
}

void
Metadata::setHaplotypes(size_type n)
{
  this->haplotype_count = n;
}

void
Metadata::setContigs(size_type n)
{
  if(this->hasContigNames())
  {
    std::cerr << "Metadata::setContigs(): Warning: Changing contig count without changing contig names" << std::endl;
  }
  this->contig_count = n;
}

std::vector<size_type>
Metadata::findPaths(size_type sample_id, size_type contig_id) const
{
  std::vector<size_type> result;
  for(size_type i = 0; i < this->paths(); i++)
  {
    if(this->path(i).sample == sample_id && this->path(i).contig == contig_id)
    {
      result.push_back(i);
    }
  }
  return result;
}

std::vector<size_type>
Metadata::pathsForSample(size_type sample_id) const
{
  std::vector<size_type> result;
  for(size_type i = 0; i < this->paths(); i++)
  {
    if(this->path(i).sample == sample_id)
    {
      result.push_back(i);
    }
  }

  return result;
}

std::vector<size_type>
Metadata::pathsForContig(size_type contig_id) const
{
  std::vector<size_type> result;
  for(size_type i = 0; i < this->paths(); i++)
  {
    if(this->path(i).contig == contig_id)
    {
      result.push_back(i);
    }
  }
  return result;
}

void
Metadata::addPath(const PathName& path)
{
  this->set(FLAG_PATH_NAMES);
  this->path_names.push_back(path);
}

void
Metadata::clearPathNames()
{
  this->unset(FLAG_PATH_NAMES);
  this->path_names = std::vector<PathName>();
}

void
Metadata::setSamples(const std::vector<std::string>& names)
{
  if(names.empty())
  {
    this->clearSampleNames();
    return;
  }

  this->setSamples(names.size());
  this->set(FLAG_SAMPLE_NAMES);
  this->sample_names = Dictionary(names);
}

void
Metadata::clearSampleNames()
{
  this->unset(FLAG_SAMPLE_NAMES);
  this->sample_names.clear();
}

void
Metadata::setContigs(const std::vector<std::string>& names)
{
  if(names.empty())
  {
    this->clearContigNames();
    return;
  }

  this->setContigs(names.size());
  this->set(FLAG_CONTIG_NAMES);
  this->contig_names = Dictionary(names);
}

void
Metadata::clearContigNames()
{
  this->unset(FLAG_CONTIG_NAMES);
  this->contig_names.clear();
}

void
Metadata::merge(const Metadata& source, bool same_samples, bool same_contigs)
{
  size_type source_sample_offset = 0, source_contig_offset = 0;

  // Merge samples and haplotypes.
  if(same_samples)
  {
    if(this->samples() != source.samples() || this->haplotypes() != source.haplotypes())
    {
      std::cerr << "Metadata::merge(): Warning: Sample/haplotype counts do not match" << std::endl;
    }
    if(!(this->hasSampleNames()) && source.hasSampleNames())
    {
      if(Verbosity::level >= Verbosity::EXTENDED)
      {
        std::cerr << "Metadata::merge(): Taking sample names from the source" << std::endl;
      }
      this->sample_names = source.sample_names;
    }
  }
  else
  {
    source_sample_offset = this->samples();
    this->sample_count += source.samples();
    this->haplotype_count += source.haplotypes();
    if(this->hasSampleNames())
    {
      if(source.hasSampleNames())
      {
        this->sample_names.append(source.sample_names);
      }
      else
      {
        if(Verbosity::level >= Verbosity::EXTENDED)
        {
          std::cerr << "Metadata::merge(): Clearing sample names: the source has no sample names" << std::endl;
        }
        this->clearSampleNames();
      }
    }
  }

  // Merge contigs.
  if(same_contigs)
  {
    if(this->contigs() != source.contigs())
    {
      std::cerr << "Metadata::merge(): Warning: Contig counts do not match" << std::endl;
    }
    if(!(this->hasContigNames()) && source.hasContigNames())
    {
      if(Verbosity::level >= Verbosity::EXTENDED)
      {
        std::cerr << "Metadata::merge(): Taking contig names from the source" << std::endl;
      }
      this->contig_names = source.contig_names;
    }
  }
  else
  {
    source_contig_offset = this->contigs();
    this->contig_count += source.contigs();
    if(this->hasContigNames())
    {
      if(source.hasContigNames())
      {
        this->contig_names.append(source.contig_names);
      }
      else
      {
        if(Verbosity::level >= Verbosity::EXTENDED)
        {
          std::cerr << "Metadata::merge(): Clearing contig names: the source has no contig names" << std::endl;
        }
        this->clearContigNames();
      }
    }
  }

  // Merge paths.
  if(this->hasPathNames())
  {
    if(source.hasPathNames())
    {
      size_type source_path_offset = this->paths();
      this->path_names.insert(this->path_names.end(), source.path_names.begin(), source.path_names.end());
      for(size_type i = source_path_offset; i < this->path_names.size(); i++)
      {
        this->path_names[i].sample += source_sample_offset;
        this->path_names[i].contig += source_contig_offset;
      }
    }
    else
    {
      if(Verbosity::level >= Verbosity::EXTENDED)
      {
        std::cerr << "Metadata::merge(): Clearing path names: the source has no path names" << std::endl;
      }
      this->clearPathNames();
    }
  }
}

void
Metadata::merge(std::vector<const Metadata*> sources, bool same_samples, bool same_contigs)
{
  for(const Metadata* source : sources) { this->merge(*source, same_samples, same_contigs); }
}

void
Metadata::clear()
{
  *this = Metadata();
}

std::ostream& operator<<(std::ostream& stream, const Metadata& metadata)
{
  if(metadata.get(Metadata::FLAG_PATH_NAMES))
  {
    stream << metadata.paths() << " paths with names, ";
  }

  stream << metadata.samples() << " samples";
  if(metadata.get(Metadata::FLAG_SAMPLE_NAMES)) { stream << " with names"; }
  stream << ", ";

  stream << metadata.haplotypes() << " haplotypes, ";

  stream << metadata.contigs() << " contigs";
  if(metadata.get(Metadata::FLAG_CONTIG_NAMES)) { stream << " with names"; }

  return stream;
}

//------------------------------------------------------------------------------

} // namespace gbwt
