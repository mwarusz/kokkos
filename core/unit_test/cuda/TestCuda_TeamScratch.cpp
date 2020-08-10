/*
//@HEADER
// ************************************************************************
//
//                        Kokkos v. 3.0
//       Copyright (2020) National Technology & Engineering
//               Solutions of Sandia, LLC (NTESS).
//
// Under the terms of Contract DE-NA0003525 with NTESS,
// the U.S. Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY NTESS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL NTESS OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact Christian R. Trott (crtrott@sandia.gov)
//
// ************************************************************************
//@HEADER
*/

#include <cuda/TestCuda_Category.hpp>
#include <TestTeam.hpp>

namespace Test {

namespace Impl {

void cuda_stream_scratch_test_one(
    int N, int T, int M_base, Kokkos::View<int64_t, Kokkos::CudaSpace> counter,
    Kokkos::Cuda cuda, int tid) {
  int M = M_base + tid * 5;
  Kokkos::TeamPolicy<Kokkos::Cuda> p(cuda, T, 64);
  using team_t    = Kokkos::TeamPolicy<Kokkos::Cuda>::member_type;
  using scratch_t = Kokkos::View<int64_t*, Kokkos::Cuda::scratch_memory_space>;

  int bytes = scratch_t::shmem_size(M);

  for (int r = 0; r < 15; r++) {
    Kokkos::parallel_for(
        "Run", p.set_scratch_size(1, Kokkos::PerTeam(bytes)),
        KOKKOS_LAMBDA(const team_t& team) {
          scratch_t scr(team.team_scratch(1), M);
          Kokkos::parallel_for(Kokkos::TeamThreadRange(team, 0, M),
                               [&](int i) { scr[i] = 0; });
          team.team_barrier();
          for (int i = 0; i < N; i++) {
            Kokkos::parallel_for(Kokkos::TeamThreadRange(team, 0, M),
                                 [&](int j) { scr[j] += 1; });
          }
          team.team_barrier();
          Kokkos::parallel_for(Kokkos::TeamThreadRange(team, 0, M), [&](int i) {
            if (scr[i] != N) Kokkos::atomic_increment(&counter());
          });
        });
  }
}

void cuda_stream_scratch_test(
    int N, int T, int M_base,
    Kokkos::View<int64_t, Kokkos::CudaSpace> counter) {
  int K = 4;
  cudaStream_t stream[4];
  Kokkos::Cuda cuda[4];
  for (int i = 0; i < K; i++) {
    cudaStreamCreate(&stream[i]);
    cuda[i] = Kokkos::Cuda(stream[i]);
  }
  // Test that growing scratch size in subsequent calls doesn' crash things
#if defined(KOKKOS_ENABLE_OPENMP)
#pragma omp parallel
  {
    int tid = omp_get_thread_num();
    // Limit how many threads submit
    if (tid < 4) {
      cuda_stream_scratch_test_one(N, T, M_base, counter, cuda[tid], tid);
    }
  }
#else
  for (int tid = 0; tid < K; tid++) {
    cuda_stream_scratch_test_one(N, T, M_base, counter, cuda[tid], tid);
  }
#endif
  // Test that if everything is large enough, multiple launches with different
  // scratch sizes don't step on each other
  for (int tid = K - 1; tid >= 0; tid--) {
    cuda_stream_scratch_test_one(N, T, M_base, counter, cuda[tid], tid);
  }

  Kokkos::fence();
  for (int i = 0; i < K; i++) {
    cuda[i] = Kokkos::Cuda();
    cudaStreamDestroy(stream[i]);
  }
}
}  // namespace Impl

TEST(cuda, team_scratch_1_streams) {
  int N      = 1000000;
  int T      = 10;
  int M_base = 150;

  Kokkos::View<int64_t, Kokkos::CudaSpace> counter("C");

  Impl::cuda_stream_scratch_test(N, T, M_base, counter);

  int64_t result;
  Kokkos::deep_copy(result, counter);
  ASSERT_EQ(0, result);
}

TEST(TEST_CATEGORY, team_shared_request) {
  TestSharedTeam<TEST_EXECSPACE, Kokkos::Schedule<Kokkos::Static> >();
  TestSharedTeam<TEST_EXECSPACE, Kokkos::Schedule<Kokkos::Dynamic> >();
}

TEST(TEST_CATEGORY, team_scratch_request) {
  TestScratchTeam<TEST_EXECSPACE, Kokkos::Schedule<Kokkos::Static> >();
  TestScratchTeam<TEST_EXECSPACE, Kokkos::Schedule<Kokkos::Dynamic> >();
}

#if defined(KOKKOS_ENABLE_CXX11_DISPATCH_LAMBDA)
TEST(TEST_CATEGORY, team_lambda_shared_request) {
  TestLambdaSharedTeam<Kokkos::HostSpace, TEST_EXECSPACE,
                       Kokkos::Schedule<Kokkos::Static> >();
  TestLambdaSharedTeam<Kokkos::HostSpace, TEST_EXECSPACE,
                       Kokkos::Schedule<Kokkos::Dynamic> >();
}

TEST(TEST_CATEGORY, scratch_align) { TestScratchAlignment<TEST_EXECSPACE>(); }
#endif

TEST(TEST_CATEGORY, shmem_size) { TestShmemSize<TEST_EXECSPACE>(); }

TEST(TEST_CATEGORY, multi_level_scratch) {
  TestMultiLevelScratchTeam<TEST_EXECSPACE,
                            Kokkos::Schedule<Kokkos::Static> >();
  TestMultiLevelScratchTeam<TEST_EXECSPACE,
                            Kokkos::Schedule<Kokkos::Dynamic> >();
}

}  // namespace Test
