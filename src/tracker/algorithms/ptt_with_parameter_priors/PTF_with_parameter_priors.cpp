#include "PTF_with_parameter_priors.h"

PTF_with_parameter_priors::PTF_with_parameter_priors(RandomDoer *_rndmr) {
	init_Frame();
	rndmr          = _rndmr;
    initialized    = false;
}

void PTF_with_parameter_priors::init_Frame() {
	p 			= new float[3];
    
    k1          = 0;
    k2          = 0;
    kT1         = 1;
    kT2         = 0;
    
    F           = new float*[3];
    F[0]        = new float[3];
    F[1]        = new float[3];
    F[2]        = new float[3];

    PP          = new float[9];
    sPP         = new float[9];
    
	likelihood 	= 0.0;
    lastVal     = 0.0;
    
    ptf_FOD     = new FOD_Image(*TRACKER::img_FOD);
    FOD         = new float[SH::numberOfSphericalHarmonicCoefficients];
    
    ptf_dispersion = new SCALAR_Image(*TRACKER::img_dispersion);
    
    // Initialize thread specific fiber tracking parameters
    param_maxCurvature        = TRACKER::maxCurvature;
    param_dataSupportExponent = TRACKER::dataSupportExponent;
    
}

PTF_with_parameter_priors::~PTF_with_parameter_priors() {
	delete[] p;
    
    delete[] F[0];
    delete[] F[1];
    delete[] F[2];
    delete[] F;
    
    delete[] PP;
    delete[] sPP;
    
    ptf_FOD->destroyCopy();
    ptf_dispersion->destroyCopy();
    
	delete   ptf_FOD;
    delete[] FOD;
    
}

void PTF_with_parameter_priors::initkT(PTF_with_parameter_priors *ptf) {
    kT1 = k1 - ptf->k1; // Sets the kT of the curve
    kT2 = k2 - ptf->k2;
    
    float norm = std::sqrt(kT1*kT1 + kT2*kT2);
    kT1 /= norm;
    kT2 /= norm;
    
    ptf->kT1 = kT1; // Sets the kT of the initial_curve
    ptf->kT2 = kT2;
    
    initialized = true;
    
}

void PTF_with_parameter_priors::swap(PTF_with_parameter_priors *ptf) {

    k1       =  ptf->k1;
    k2       =  ptf->k2;
    k1_cand  =  ptf->k1_cand;
    k2_cand  =  ptf->k2_cand;
    kT1      =  ptf->kT1;
    kT2      =  ptf->kT2;
    kT1_cand =  ptf->kT1_cand;
    kT2_cand =  ptf->kT2_cand;
    probe_k1 =  ptf->probe_k1;
    probe_k2 =  ptf->probe_k2;
    
	for (int i=0; i<3; i++) {
        p[i] 	= ptf->p[i];
        for (int j=0; j<3; j++) {
            F[i][j] = ptf->F[i][j];
        }
        
	}
	
	for (int i=0; i<8; i++) {
        PP[i]  = ptf->PP[i];
        sPP[i] = ptf->sPP[i];
    }
	
	likelihood 	      = ptf->likelihood;
    initFirstVal      = ptf->initFirstVal;
    lastVal           = ptf->lastVal;
    initFirstVal_cand = ptf->initFirstVal_cand;
    lastVal_cand      = ptf->lastVal_cand;
}

void PTF_with_parameter_priors::getARandomFrame() {
	rndmr->getAUnitRandomVector(F[0]);
	rndmr->getAUnitRandomPerpVector(F[2],F[0]);
	cross(F[1],F[2],F[0]);
}

void PTF_with_parameter_priors::getARandomFrame(Coordinate _seed_init_direction) {
	F[0][0] = _seed_init_direction.x;
	F[0][1] = _seed_init_direction.y;
	F[0][2] = _seed_init_direction.z;
	rndmr->getAUnitRandomPerpVector(F[2],F[0]);
	cross(F[1],F[2],F[0]);
}

// To flip PTF_with_parameter_priors parameterized curve
// flip signs of T, K1 and k1
// keep K2 and k2 as they are
void PTF_with_parameter_priors::flip() {
	walk();

	for (int i=0; i<3; i++) {
		F[0][i]  	*= -1;
		F[1][i] 	*= -1;
	}
	k1      *= -1;
    k1_cand *= -1;
    
    kT1 *= -1;
    kT2 *= -1;

	likelihood 	= 0.0;
    lastVal     = initFirstVal;
}


void PTF_with_parameter_priors::print() {
	std::cout << "p:  " << p[0] << " " << p[1] << " " << p[2] << std::endl;
	std::cout << "k:  " << getk()  << std::endl;
	std::cout << "k1: " << getk1() << std::endl;
	std::cout << "k2: " << getk2() << std::endl;
	std::cout << "T: "  << F[0][0] << " " <<  F[0][1] << " " <<  F[0][2] << std::endl;
	std::cout << "K1: " << F[1][0] << " " <<  F[1][1] << " " <<  F[1][2] << std::endl;
	std::cout << "K2: " << F[2][0] << " " <<  F[2][1] << " " <<  F[2][2] << std::endl;
	std::cout << "likelihood: " << likelihood << std::endl;
}









void PTF_with_parameter_priors::getCandidate() {
    rndmr->getARandomPointWithinDisk(&k1_cand, &k2_cand, param_maxCurvature);
    calcDataSupport();
}


void PTF_with_parameter_priors::calcDataSupport() {
    
    
    prepInitProbePropagator();

    
    // Copy initial _p and _F
    
    for (int i=0; i<3; i++) {
        _p[i] 	= p[i];
        for (int j=0; j<3; j++) {
            _F[i][j] = F[i][j];
        }
        
	}
    
    if (TRACKER::img_FOD->iseven) {
    
        likelihood = lastVal;
        
        for (int q=0; q<(TRACKER::probeQuality-1); q++) {
            
            
            for (int i=0; i<3; i++) {
                _p[i]  += PP[0]*_F[0][i] +  PP[1]*_F[1][i]  +  PP[2]*_F[2][i];
                _T[i]   = PP[3]*_F[0][i] +  PP[4]*_F[1][i]  +  PP[5]*_F[2][i];
            }
            normalize(_T);
            
            if (q<(TRACKER::probeQuality-1)) {
                
                for (int i=0; i<3; i++) {
                    _N2[i]  = PP[6]*_F[0][i] +  PP[7]*_F[1][i]  +  PP[8]*_F[2][i];
                }
                
                cross(_N1,_N2,_T);
                for (int i=0; i<3; i++) {
                    _F[0][i] =  _T[i];
                    _F[1][i] = _N1[i];
                    _F[2][i] = _N2[i];
                }
                
            }
            
            
            if (TRACKER::probeCount==1) {
                
                lastVal_cand = getFODamp(_p,_T);
                
                if ((TRACKER::checkWeakLinks==CHECKWEAKLINKS_ON) && (lastVal_cand < TRACKER::weakLinkThresh)) {
                    likelihood  = 0;
                    return;
                } else {
                    likelihood += lastVal_cand;
                }
                
                
            } else {
                
                lastVal_cand = 0;
                
                if (q==(TRACKER::probeQuality-1)) {
                    for (int i=0; i<3; i++) {
                        _N2[i]  = PP[6]*_F[0][i] +  PP[7]*_F[1][i]  +  PP[8]*_F[2][i];
                    }
                    cross(_N1,_N2,_T);
                }
                
                
                for (float c=0; c<TRACKER::probeCount; c++) {
                    
                    float pp[3];
                    
                    for (int i=0; i<3; i++) {
                        pp[i] = _p[i] + _N1[i]*TRACKER::probeRadius*std::cos(c*TRACKER::angularSeparation) + _N2[i]*TRACKER::probeRadius*std::sin(c*TRACKER::angularSeparation);
                    }
                    
                    float val = getFODamp(pp,_T);
                    
                    if ((TRACKER::checkWeakLinks==CHECKWEAKLINKS_ON) && (val < TRACKER::weakLinkThresh)) {
                        likelihood    = 0;
                        return;
                    } else {
                        lastVal_cand += val;
                    }

                } 
                
                likelihood += lastVal_cand;
                
            }
            
        }
        
    } else {
        
        likelihood = 0;
        
        float pn[3];
        float Tb[3];
        float Te[3];
        
        for (int q=0; q<(TRACKER::probeQuality-1); q++) {
            
            
            for (int i=0; i<3; i++) {
                pn[i]  = _p[i] + PP[0]*_F[0][i] +  PP[1]*_F[1][i]  +  PP[2]*_F[2][i];
                _T[i]  =         PP[3]*_F[0][i] +  PP[4]*_F[1][i]  +  PP[5]*_F[2][i];
               _N2[i]  =         PP[6]*_F[0][i] +  PP[7]*_F[1][i]  +  PP[8]*_F[2][i];
                Tb[i]  = pn[i] - _p[i];
            }
            normalize(_T);
            cross(_N1,_N2,_T);
            normalize(Tb);
            for (int i=0; i<3; i++) {
                Te[i]  = -Tb[i];
            }
            
            
            if (TRACKER::probeCount==1) {
                
                float link = (getFODamp(_p,Tb) + getFODamp(pn,Te))/float(2.0);
                
                if ((TRACKER::checkWeakLinks==CHECKWEAKLINKS_ON) && (link < TRACKER::weakLinkThresh)) {
                    likelihood  = 0;
                    return;
                } else {
                    likelihood += link;
                }
                
            } else {
                
                for (float c=0; c<TRACKER::probeCount; c++) {
                    
                    float pp[3];
                    float ppn[3];
                    
                    for (int i=0; i<3; i++) {
                        pp[i]  =  _p[i] + _F[1][i]*TRACKER::probeRadius*std::cos(c*TRACKER::angularSeparation) + _F[2][i]*TRACKER::probeRadius*std::sin(c*TRACKER::angularSeparation);
                        ppn[i] =  pn[i] +   _N1[i]*TRACKER::probeRadius*std::cos(c*TRACKER::angularSeparation) +   _N2[i]*TRACKER::probeRadius*std::sin(c*TRACKER::angularSeparation);
                        Tb[i]  = ppn[i] -    pp[i];
                    }
                    
                    normalize(Tb);
                    for (int i=0; i<3; i++) {
                        Te[i]  = -Tb[i];
                    }
                    
                    float link = (getFODamp(pp,Tb) + getFODamp(ppn,Te))/float(2.0);
                    
                    if ((TRACKER::checkWeakLinks==CHECKWEAKLINKS_ON) && (link < TRACKER::weakLinkThresh)) {
                        likelihood  = 0;
                        return;
                    } else {
                        likelihood += link;
                    }

                }
                
            }
            
            // Update _F here
            if (q<(TRACKER::probeQuality-1)) {
                for (int i=0; i<3; i++) {
                       _p[i] = pn[i];
                    _F[0][i] =  _T[i];
                    _F[1][i] = _N1[i];
                    _F[2][i] = _N2[i];
                }
            }
            
        }
        
        
        
    }

    likelihood *= TRACKER::probeNormalizer;
    if (TRACKER::dataSupportExponent != 1)
        likelihood  = std::pow(likelihood,TRACKER::dataSupportExponent);

}





void PTF_with_parameter_priors::getInitCandidate() {
    
    rndmr->getARandomPointWithinDisk(&k1_cand, &k2_cand, param_maxCurvature);
    k1 = k1_cand;
    k2 = k2_cand;
    
    if (TRACKER::img_FOD->iseven) {
        
        // First part of the probe
        likelihood        = 0.0;
        getCurrentCurve(p,F);
        
        if (TRACKER::probeCount==1) {
            likelihood = getFODamp(p,F[0]);
        } else {
            
            for (float c=0; c<TRACKER::probeCount; c++) {
                        
                float pp[3];
                
                for (int i=0; i<3; i++) {
                    pp[i] = p[i] + F[1][i]*TRACKER::probeRadius*std::cos(c*TRACKER::angularSeparation) + F[2][i]*TRACKER::probeRadius*std::sin(c*TRACKER::angularSeparation);
                }
                
                float val = getFODamp(pp,F[0]);
                
                if ((TRACKER::checkWeakLinks==CHECKWEAKLINKS_ON) && (val < TRACKER::weakLinkThresh)) {
                    likelihood  = 0;
                    return;
                } else {
                    likelihood += val;
                }

            }
            
        }
        
        initFirstVal_cand = likelihood;
        lastVal_cand      = likelihood;
    }
    
    // initFirstVal_cand and lastVal_cand are not used in the asymmetric FOD case
    
    calcDataSupport();

}

void PTF_with_parameter_priors::updateTrackingParameters() {

     ptf_dispersion->getVal(p,&param_maxCurvature);
     param_maxCurvature *= 10;
}

