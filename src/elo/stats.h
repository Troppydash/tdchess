#pragma once

#include "pentanomial.h"
#include <boost/math/tools/roots.hpp>
#include <cmath>
#include <string>
#include <vector>

// copied from https://github.com/official-stockfish/fishtest/blob/master/server/fishtest/stats/sprt.py

namespace brownian
{
inline double Phi(double x)
{
    return 0.5 * std::erfc(-x / std::sqrt(2.0));
}

inline double U(int n, double gamma, double A, double y)
{
    const double pi = M_PI;

    return (2.0 * A * gamma * std::sin(pi * n * y / A) - 2.0 * pi * n * std::cos(pi * n * y / A)) /
           (A * A * gamma * gamma + pi * pi * n * n);
}

class Brownian
{
  public:
    explicit Brownian(double a_ = -1.0, double b_ = 1.0, double mu_ = 0.0, double sigma_ = 0.005)
        : a(a_), b(b_), mu(mu_), sigma(sigma_), sigma2(sigma_ * sigma_)
    {
    }

    double outcome_cdf(double T, double y) const
    {
        const double A = b - a;
        const double gamma = mu / sigma2;

        double ret;
        if (sigma2 * T / (A * A) < 1e-2 || std::abs(gamma * A) > 15.0)
            ret = outcome_cdf_alt2(T, y);
        else
            ret = outcome_cdf_alt1(T, y);

        assert(ret >= -1e-3 && ret <= 1.0 + 1e-3);
        return ret;
    }

  private:
    double outcome_cdf_alt1(double T, double y) const
    {
        const double pi = M_PI;

        const double A = b - a;
        const double x = -a;
        y -= a;

        const double gamma = mu / sigma2;

        double s = 0.0;
        int n = 1;

        const double lambda_1 = (pi / A) * (pi / A) * sigma2 / 2.0 + (mu * mu / sigma2) / 2.0;

        const double t0 = std::exp(-lambda_1 * T - x * gamma + y * gamma);

        while (true)
        {
            const double lambda_n = (n * pi / A) * (n * pi / A) * sigma2 / 2.0 + (mu * mu / sigma2) / 2.0;

            const double t1 = std::exp(-(lambda_n - lambda_1) * T);
            const double t3 = U(n, gamma, A, y);
            const double t4 = std::sin(n * pi * x / A);

            s += t1 * t3 * t4;

            if (std::abs(t0 * t1 * t3) <= 1e-9)
                break;

            ++n;
        }

        double pre;
        if (gamma * A > 30.0)
        {
            // avoid numerical overflow
            pre = std::exp(-2.0 * gamma * x);
        }
        else if (std::abs(gamma * A) < 1e-8)
        {
            // avoid division by zero
            pre = (A - x) / A;
        }
        else
        {
            pre = (1.0 - std::exp(2.0 * gamma * (A - x))) / (1.0 - std::exp(2.0 * gamma * A));
        }

        return pre + t0 * s;
    }

    double outcome_cdf_alt2(double T, double y) const
    {
        const double denom = std::sqrt(T * sigma2);
        const double offset = mu * T;
        const double gamma = mu / sigma2;

        const double z = (y - offset) / denom;
        const double za = (-y + offset + 2.0 * a) / denom;
        const double zb = (y - offset - 2.0 * b) / denom;

        const double t1 = Phi(z);

        double t2;
        if (gamma * a >= 5.0)
        {
            t2 = -std::exp(-(za * za) / 2.0 + 2.0 * gamma * a) / std::sqrt(2.0 * M_PI) *
                 (1.0 / za - 1.0 / (za * za * za));
        }
        else
        {
            t2 = std::exp(2.0 * gamma * a) * Phi(za);
        }

        double t3;
        if (gamma * b >= 5.0)
        {
            t3 = -std::exp(-(zb * zb) / 2.0 + 2.0 * gamma * b) / std::sqrt(2.0 * M_PI) *
                 (1.0 / zb - 1.0 / (zb * zb * zb));
        }
        else
        {
            t3 = std::exp(2.0 * gamma * b) * Phi(zb);
        }

        return t1 + t2 - t3;
    }

  private:
    double a;
    double b;
    double mu;
    double sigma;
    double sigma2;
};
} // namespace brownian

namespace llr
{

using pdf_dist = std::vector<std::pair<double, double>>;

inline std::pair<double, double> stats(const pdf_dist &pdf)
{
    double mean = 0.0;
    double var = 0.0;

    for (auto &[value, prob] : pdf)
    {
        mean += prob * value;
    }

    for (auto &[value, prob] : pdf)
    {
        var += prob * (value - mean) * (value - mean);
    }

    return {mean, var};
}

inline double secular(const pdf_dist &pdf)
{
    // find min/max ai
    double v = pdf.front().first;
    double w = pdf.back().first;
    for (const auto &[ai, pi] : pdf)
    {
        v = std::min(v, ai);
        w = std::max(w, ai);
    }

    assert(v * w < 0);

    const double epsilon = 1e-9;
    double lower_bound = -1.0 / w + epsilon;
    double upper_bound = -1.0 / v - epsilon;

    auto f = [&](double x) {
        double s = 0.0;
        for (const auto &[ai, pi] : pdf)
            s += pi * ai / (1.0 + x * ai);
        return s;
    };

    uintmax_t max_iter = 1000;
    boost::math::tools::eps_tolerance<double> tol(50);
    auto result = boost::math::tools::toms748_solve(f, lower_bound, upper_bound, tol, max_iter);
    return (result.first + result.second) / 2.0;
}

inline pdf_dist mle_expected(const pdf_dist &pdf_hat, double s)
{
    pdf_dist pdf1;
    for (auto &[ai, pi] : pdf_hat)
        pdf1.push_back({ai - s, pi});

    double x = secular(pdf1);
    pdf_dist pdf_MLE;
    for (auto &[ai, pi] : pdf_hat)
        pdf_MLE.push_back({ai, pi / (1 + x * (ai - s))});

    auto [s_, _] = stats(pdf_MLE);
    assert(std::abs(s - s_) < 1e-6);
    return pdf_MLE;
}

inline pdf_dist llr_jumps(const pdf_dist &pdf, double s0, double s1)
{
    auto pdf0 = mle_expected(pdf, s0);
    auto pdf1 = mle_expected(pdf, s1);

    pdf_dist jumps;
    for (size_t i = 0; i < pdf.size(); ++i)
    {
        jumps.push_back({std::log(pdf1[i].second) - std::log(pdf0[i].second), pdf[i].second});
    }

    return jumps;
}

inline std::pair<double, double> llr_drift_variance(const pdf_dist &pdf, double s0, double s1,
                                                    std::optional<double> s = std::nullopt)
{
    auto [s_, v_] = stats(pdf);

    double mean, var;
    if (!s)
    {
        mean = s_;
        var = v_;
    }
    else
    {
        mean = *s;
        var = v_ + (*s - s_) * (*s - s_);
    }

    double mu = (mean - (s0 + s1) / 2.0) * (s1 - s0) / var;
    double var_llr = (s1 - s0) * (s1 - s0) / var;

    return {mu, var_llr};
}

inline std::vector<double> regularize(const std::vector<double> &values)
{
    std::vector<double> copied{values};
    double epsilon = 1e-3;
    for (size_t i = 0; i < values.size(); ++i)
    {
        if (copied[i] == 0)
            copied[i] = epsilon;
    }

    return copied;
}

inline std::pair<double, pdf_dist> results_to_pdf(const std::vector<double> &results)
{
    auto regularized_results = regularize(results);

    double N = 0.0;
    for (auto r : regularized_results)
        N += r;

    size_t count = regularized_results.size();
    std::vector<std::pair<double, double>> pdf;
    for (size_t i = 0; i < count; ++i)
        pdf.push_back({static_cast<double>(i) / (count - 1), regularized_results[i] / N});

    return {N, pdf};
}

inline double L(double elo)
{
    return 1.0 / (1.0 + std::pow(10.0, -elo / 400.0));
}

} // namespace llr

struct sprt
{
    std::string elo_model;
    double a;
    double b;
    double elo0;
    double elo1;

    bool clamped = false;
    double T = 0;
    double llr = 0;
    llr::pdf_dist pdf;
    double s0 = 0;
    double s1 = 0;

    explicit sprt(std::string elo_model = "logistic", double alpha = 0.05, double beta = 0.05, double elo0 = 0,
                  double elo1 = 5)
        : elo_model(std::move(elo_model)), a(std::log(beta / (1 - alpha))), b(std::log((1 - beta) / alpha)), elo0(elo0),
          elo1(elo1)
    {
    }

    double elo_to_score(double elo)
    {
        return llr::L(elo);
    }

    void set_state(const pentanomial &results)
    {
        auto [N, pdf_] = llr::results_to_pdf(results.to_raw());
        this->pdf = pdf_;
        s0 = elo_to_score(elo0);
        s1 = elo_to_score(elo1);

        auto [mu_llr, var_llr] = llr::llr_drift_variance(pdf, s0, s1);

        llr = N * mu_llr;
        T = N;

        double slope = llr / N;
        if (llr > 1.03 * b || llr < 1.03 * a)
            clamped = true;

        if (llr < a)
        {
            T = a / slope;
            llr = a;
        }
        else if (llr > b)
        {
            T = b / slope;
            llr = b;
        }
    }

    double outcome_prob(double elo) const
    {
        double s = llr::L(elo);
        auto [mu_llr, var_llr] = llr::llr_drift_variance(pdf, s0, s1, s);
        double sigma_llr = std::sqrt(var_llr);
        return brownian::Brownian{a, b, mu_llr, sigma_llr}.outcome_cdf(T, llr);
    }

    double lower_cb(double p)
    {
        double avg_elo = (elo0 + elo1) / 2.0;
        double delta = elo1 - elo0;
        double N = 30;

        while (true)
        {
            double elo0 = std::max(avg_elo - N * delta, -1000.0);
            double elo1 = std::min(avg_elo + N * delta, 1000.0);

            try
            {
                uintmax_t max_iter = 1000;
                auto f = [&](double elo) { return outcome_prob(elo) - (1 - p); };
                boost::math::tools::eps_tolerance<double> tol(50);
                auto result = boost::math::tools::toms748_solve(f, elo0, elo1, tol, max_iter);
                return (result.first + result.second) / 2.0;
            }
            catch (boost::wrapexcept<std::domain_error> &e)
            {
                if (elo0 > -1000 || elo1 < 1000)
                {
                    N *= 2;
                    continue;
                }
                if (outcome_prob(elo0) - (1 - p) > 0)
                    return elo1;
                return elo0;
            }
        }
    }

    void analytics(double p = 0.05)
    {
        std::cout << "clamped " << clamped << "\n"
                  << "a " << a << "\n"
                  << "b " << b << "\n"
                  << "expected elo " << lower_cb(0.5) << "\n"
                  << "elo CI-" << (1 - p) << " " << lower_cb(p / 2.0) << ", " << lower_cb(1.0 - p / 2.0) << "\n"
                  << "P(elo=0) LOS " << outcome_prob(0.0) << "\n"
                  << "LLR " << llr << "\n";
    }
};