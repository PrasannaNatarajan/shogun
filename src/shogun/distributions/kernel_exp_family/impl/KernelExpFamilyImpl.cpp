/*
 * Copyright (c) The Shogun Machine Learning Toolbox
 * Written (w) 2016 Heiko Strathmann
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are those
 * of the authors and should not be interpreted as representing official policies,
 * either expressed or implied, of the Shogun Development Team.
 */

#include <shogun/lib/config.h>
#include <shogun/lib/SGMatrix.h>
#include <shogun/lib/SGVector.h>
#include <shogun/mathematics/eigen3.h>
#include <shogun/mathematics/Math.h>

#include "KernelExpFamilyImpl.h"

using namespace shogun;
using namespace Eigen;

index_t KernelExpFamilyImpl::get_num_dimensions()
{
	return m_data.num_rows;
}

index_t KernelExpFamilyImpl::get_num_data()
{
	return m_data.num_cols;
}

KernelExpFamilyImpl::KernelExpFamilyImpl(SGMatrix<float64_t> data, float64_t sigma, float64_t lambda)
{
	m_data = data;
	m_sigma = sigma;
	m_lambda = lambda;
}

float64_t KernelExpFamilyImpl::kernel(index_t idx_a, index_t idx_b)
{
	auto D = get_num_dimensions();

	Map<VectorXd> x(m_data.get_column_vector(idx_a), D);
	Map<VectorXd> y(m_data.get_column_vector(idx_b), D);
	return CMath::exp(-(x-y).squaredNorm() / m_sigma);
}

SGMatrix<float64_t> KernelExpFamilyImpl::kernel_dx_dx_dy(index_t idx_a, index_t idx_b)
{
	auto D = get_num_dimensions();
	Map<VectorXd> x(m_data.get_column_vector(idx_a), D);
	Map<VectorXd> y(m_data.get_column_vector(idx_b), D);
	auto diff=x-y;
	auto diff2 = diff.array().pow(2).matrix();
	auto k=CMath::exp(-diff2.sum() / m_sigma);

	SGMatrix<float64_t> result(D, D);
	Map<MatrixXd> eigen_result(result.matrix, D, D);
	eigen_result = pow(2./m_sigma,3) * k * (diff2*diff.transpose());
	eigen_result -= pow(2./m_sigma,2) * k * 2* diff.asDiagonal();
	eigen_result.rowwise() -=  (pow(2./m_sigma,2) * k * diff).transpose();

	return result;
}

SGMatrix<float64_t> KernelExpFamilyImpl::kernel_dx_dx_dy_dy(index_t idx_a, index_t idx_b)
{
	auto D = get_num_dimensions();
	Map<VectorXd> x(m_data.get_column_vector(idx_a), D);
	Map<VectorXd> y(m_data.get_column_vector(idx_b), D);
	VectorXd diff2 = (x-y).array().pow(2).matrix();

	//k = gaussian_kernel(x_2d, y_2d, sigma)
	auto k=CMath::exp(-diff2.sum() / m_sigma);

	auto factor = k*pow(2.0/m_sigma, 3);
	SGMatrix<float64_t> result(D, D);
	Map<MatrixXd> eigen_result(result.matrix, D, D);

	//term1 = k * np.outer((x - y), (x - y)) ** 2 * (2.0/sigma)**4
	eigen_result = k*pow(2.0/m_sigma, 4) * (diff2*diff2.transpose());

	//term2 = k * 6 * np.diag((x - y) ** 2) * (2.0/sigma)**3  # diagonal (x-y)
	eigen_result.diagonal() -= 6*factor*diff2;

	//term3 = (1 - np.eye(d)) * k * np.tile((x - y), [d, 1]).T ** 2 * (2.0/sigma)**3  # (x_i-y_i)^2 off-diagonal
	diff2 *= factor;
	eigen_result.rowwise() -=  diff2.transpose();
	eigen_result.colwise() -=  diff2;
	eigen_result.diagonal() += 2*diff2;

	// term5 = k * (1 + 2 * np.eye(d)) * (2.0/sigma)**2
	factor = k*pow(2.0/m_sigma, 2);
	eigen_result.diagonal().array() += 2*factor;
	eigen_result.array() += factor;

	// return term1 - term2 - term3 - term3.T + term5
	return result;
}

SGMatrix<float64_t> KernelExpFamilyImpl::kernel_hessian(index_t idx_a, index_t idx_b)
{
	auto D = get_num_dimensions();
	Map<VectorXd> x(m_data.get_column_vector(idx_a), D);
	Map<VectorXd> y(m_data.get_column_vector(idx_b), D);
	auto diff = x-y;

	//k = gaussian_kernel(x_2d, y_2d, sigma)
	auto k=CMath::exp(-diff.array().pow(2).sum() / m_sigma);

	SGMatrix<float64_t> result(D, D);
	Map<MatrixXd> eigen_result(result.matrix, D, D);

	// H = k*(2*np.eye(d)/sigma - 4*np.outer(differences, differences)/sigma**2)
	eigen_result = -diff*diff.transpose() / pow(m_sigma, 2) * k * 4;
	eigen_result.diagonal().array() += 2*k/m_sigma;

	return result;
}

SGVector<float64_t> KernelExpFamilyImpl::kernel_dx_dx(const SGVector<float64_t>& a, index_t idx_b)
{
	auto D = get_num_dimensions();
	Map<VectorXd> eigen_a(a.vector, D);
	Map<VectorXd> eigen_b(m_data.get_column_vector(idx_b), D);
	auto sq_diff = (eigen_a-eigen_b).array().pow(2);

	auto k=CMath::exp(-sq_diff.sum() / m_sigma);

	SGVector<float64_t> result(D);
	Map<VectorXd> eigen_result(result.vector, D);

    // k.T * (sq_differences*(2.0 / sigma)**2 - 2.0/sigma)
	eigen_result = k*(sq_diff*pow(2.0/m_sigma, 2) -2.0/m_sigma);

	return result;
}

SGMatrix<float64_t> KernelExpFamilyImpl::kernel_hessian_all()
{
	auto D = get_num_dimensions();
	auto N = get_num_data();
	auto ND = N*D;
	SGMatrix<float64_t> result(ND,ND);
	Map<MatrixXd> eigen_result(result.matrix, ND,ND);

#pragma omp for
	for (auto idx_a=0; idx_a<N; idx_a++)
		for (auto idx_b=0; idx_b<N; idx_b++)
		{
			auto r_start = idx_a*D;
			auto c_start = idx_b*D;
			SGMatrix<float64_t> h=kernel_hessian(idx_a, idx_b);
			eigen_result.block(r_start, c_start, D, D) = Map<MatrixXd>(h.matrix, D, D);
			eigen_result.block(c_start, r_start, D, D) = eigen_result.block(r_start, c_start, D, D);
		}

	return result;
}

SGVector<float64_t> KernelExpFamilyImpl::kernel_dx(const SGVector<float64_t>& a, index_t idx_b)
{
	auto D = get_num_dimensions();
	Map<VectorXd> eigen_a(a.vector, D);
	Map<VectorXd> eigen_b(m_data.get_column_vector(idx_b), D);

	//k = gaussian_kernel(x_2d, y_2d, sigma)
	auto diff = eigen_b-eigen_a;
	auto k=CMath::exp(-diff.squaredNorm() / m_sigma);

	SGVector<float64_t> gradient(D);
	Map<VectorXd> eigen_gradient(gradient.vector, D);
	eigen_gradient = 2*k*diff/m_sigma;
	return gradient;
}

SGVector<float64_t> KernelExpFamilyImpl::compute_h()
{
	auto D = get_num_dimensions();
	auto N = get_num_data();
	auto ND = N*D;
	SGVector<float64_t> h(ND);
	Map<VectorXd> eigen_h(h.vector, ND);
	eigen_h = VectorXd::Zero(ND);

#pragma omp for
	for (auto idx_b=0; idx_b<N; idx_b++)
		for (auto idx_a=0; idx_a<N; idx_a++)
		{
			// TODO optimise, no need to store matrix
			SGMatrix<float64_t> temp = kernel_dx_dx_dy(idx_a, idx_b);
			eigen_h.segment(idx_b*D, D) += Map<MatrixXd>(temp.matrix, D,D).colwise().sum();
		}

	eigen_h /= N;

	return h;
}

float64_t KernelExpFamilyImpl::compute_xi_norm_2()
{
	auto N = get_num_data();
	auto D = get_num_dimensions();
	float64_t xi_norm_2=0;

#pragma omp for
	for (auto idx_a=0; idx_a<N; idx_a++)
		for (auto idx_b=0; idx_b<N; idx_b++)
		{
			// TODO optimise, no need to store matrix
			SGMatrix<float64_t> temp=kernel_dx_dx_dy_dy(idx_a, idx_b);
			xi_norm_2 += Map<MatrixXd>(temp.matrix, D, D).sum();
		}

	xi_norm_2 /= (N*N);

	return xi_norm_2;
}

std::pair<SGMatrix<float64_t>, SGVector<float64_t>> KernelExpFamilyImpl::build_system()
{
	auto D = get_num_dimensions();
	auto N = get_num_data();
	auto ND = N*D;
	SGMatrix<float64_t> A(ND+1,ND+1);
	Map<MatrixXd> eigen_A(A.matrix, ND+1,ND+1);
	SGVector<float64_t> b(ND+1);
	Map<VectorXd> eigen_b(b.vector, ND+1);

	auto h = compute_h();
	auto eigen_h=Map<VectorXd>(h.vector, ND);
	auto all_hessians = kernel_hessian_all();
	auto eigen_all_hessians = Map<MatrixXd>(all_hessians.matrix, ND, ND);
	auto xi_norm_2 = compute_xi_norm_2();

	// A[0, 0] = np.dot(h, h) / n + lmbda * xi_norm_2
	A(0,0) = eigen_h.squaredNorm() / N + m_lambda * xi_norm_2;

	// A[1:, 1:] = np.dot(all_hessians, all_hessians) / N + lmbda * all_hessians
	eigen_A.block(1,1,ND,ND)=eigen_all_hessians*eigen_all_hessians / N + m_lambda*eigen_all_hessians;

	// A[0, 1:] = np.dot(h, all_hessians) / n + lmbda * h; A[1:, 0] = A[0, 1:]
	eigen_A.row(0).segment(1, ND) = eigen_all_hessians*eigen_h / N + m_lambda*eigen_h;
	eigen_A.col(0).segment(1, ND) = eigen_A.row(0).segment(1, ND);

	// b[0] = -xi_norm_2; b[1:] = -h.reshape(-1)
	b[0] = -xi_norm_2;
	eigen_b.segment(1, ND) = -eigen_h;

	return std::pair<SGMatrix<float64_t>, SGVector<float64_t>>(A, b);
}

void KernelExpFamilyImpl::fit()
{
	auto D = get_num_dimensions();
	auto N = get_num_data();
	auto ND = N*D;

	auto A_b = build_system();
	auto eigen_A = Map<MatrixXd>(A_b.first.matrix, ND+1, ND+1);
	auto eigen_b = Map<VectorXd>(A_b.second.vector, ND+1);

	m_alpha_beta = SGVector<float64_t>(ND+1);
	auto eigen_alpha_beta = Map<VectorXd>(m_alpha_beta.vector, ND+1);

	eigen_alpha_beta = eigen_A.ldlt().solve(eigen_b);
}

float64_t KernelExpFamilyImpl::log_pdf(const SGVector<float64_t>& x)
{
	auto D = get_num_dimensions();
	auto N = get_num_data();

	float64_t xi = 0;
	float64_t beta_sum = 0;

	Map<VectorXd> eigen_alpha_beta(m_alpha_beta.vector, N*D+1);
	for (auto idx_a=0; idx_a<N; idx_a++)
	{
		SGVector<float64_t> k=kernel_dx_dx(x, idx_a);
		Map<VectorXd> eigen_k(k.vector, D);
		xi += eigen_k.sum() / N;

		auto grad_x_xa = kernel_dx(x, idx_a);
		Map<VectorXd> eigen_grad_x_xa(grad_x_xa.vector, D);

		// betasum += np.dot(gradient_x_xa, beta[a, :])
		beta_sum += eigen_grad_x_xa.transpose()*eigen_alpha_beta.segment(1+idx_a*D, D);

	}
	return m_alpha_beta[0]*xi + beta_sum;
}

SGVector<float64_t> KernelExpFamilyImpl::grad(const SGVector<float64_t>& x)
{
	auto D = get_num_dimensions();
	auto N = get_num_data();

	SGVector<float64_t> xi_grad(D);
	SGVector<float64_t> beta_sum_grad(D);
	Map<VectorXd> eigen_xi_grad(xi_grad.vector, D);
	Map<VectorXd> eigen_beta_sum_grad(beta_sum_grad.vector, D);
	eigen_xi_grad = VectorXd::Zero(D);
	eigen_beta_sum_grad.array() = VectorXd::Zero(D);

	Map<VectorXd> eigen_alpha_beta(m_alpha_beta.vector, N*D+1);
	for (auto a=0; a<N; a++)
	{
		SGMatrix<float64_t> g=kernel_dx_i_dx_i_dx_j(x, a);
		Map<MatrixXd> eigen_g(g.matrix, D, D);
		eigen_xi_grad += eigen_g.colwise().sum() / N;

		// left_arg_hessian = gaussian_kernel_dx_i_dx_j(x, x_a, sigma)
		// betasum_grad += beta[a, :].dot(left_arg_hessian)
		auto left_arg_hessian = kernel_dx_i_dx_j(x, a);
		Map<MatrixXd> eigen_left_arg_hessian(left_arg_hessian.matrix, D, D);
		eigen_beta_sum_grad += eigen_left_arg_hessian*eigen_alpha_beta.segment(1+a*D, D).matrix();
	}

	// return alpha * xi_grad + betasum_grad
	eigen_xi_grad *= m_alpha_beta[0];
	return xi_grad + beta_sum_grad;
}

SGMatrix<float64_t> KernelExpFamilyImpl::kernel_dx_i_dx_i_dx_j(const SGVector<float64_t>& a, index_t idx_b)
{
	auto D = get_num_dimensions();
	Map<VectorXd> eigen_a(a.vector, D);
	Map<VectorXd> eigen_b(m_data.get_column_vector(idx_b), D);
	auto diff = eigen_b-eigen_a;
	auto sq_diff = diff.array().pow(2).matrix();
	auto k=CMath::exp(-sq_diff.sum() / m_sigma);

	SGMatrix<float64_t> result(D, D);
	Map<MatrixXd> eigen_result(result.matrix, D, D);

	// pairwise_dist_squared_i = np.outer((y-x)**2, y-x)
    // term1 = k*pairwise_dist_squared_i * (2.0/sigma)**3
	eigen_result = sq_diff*diff.transpose();
	eigen_result *= k* pow(2.0/m_sigma, 3);

	// row_repeated_distances = np.tile(y-x, [d,1])
	// term2 = k*row_repeated_distances * (2.0/sigma)**2
	eigen_result.rowwise() -= k * diff.transpose() * pow(2.0/m_sigma, 2);

	// term3 = term2*2*np.eye(d)
	eigen_result.diagonal() -= 2* k * diff * pow(2.0/m_sigma, 2);

	// return term1 - term2 - term3
	return result;
}

SGMatrix<float64_t> KernelExpFamilyImpl::kernel_dx_i_dx_j(const SGVector<float64_t>& a, index_t idx_b)
{
	auto D = get_num_dimensions();
	Map<VectorXd> eigen_a(a.vector, D);
	Map<VectorXd> eigen_b(m_data.get_column_vector(idx_b), D);
	auto diff = eigen_b-eigen_a;
	auto k=CMath::exp(-diff.array().pow(2).sum() / m_sigma);

	SGMatrix<float64_t> result(D, D);
	Map<MatrixXd> eigen_result(result.matrix, D, D);

	// pairwise_dist = np.outer(y-x, y-x)
    // term1 = k*pairwise_dist * (2.0/sigma)**2
	eigen_result = diff*diff.transpose();
	eigen_result *= k * pow(2.0/m_sigma, 2);

	// term2 = k*np.eye(d) * (2.0/sigma)
	eigen_result.diagonal().array() -= k * 2.0/m_sigma;

	// return term1 - term2
	return result;
}